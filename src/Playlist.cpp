#include <chrono>
#include <cinttypes>
#include <kodi/AddonBase.h>
#include <kodi/Filesystem.h>
#include "Playlist.hpp"
#include "HttpEngine.hpp"
#include "helpers.h"

using namespace std::chrono;

namespace Buffers {

std::string ToAbsoluteUrl(const std::string& url, const std::string& baseUrl)
{
    const std::string_view schemes[] = {"http://", "https://"};
    
    if (url.find("://") != std::string::npos)
        return url;

    for (auto scheme : schemes) {
        size_t pos = baseUrl.find(scheme);
        if (pos != std::string::npos) {
            pos += scheme.size();
            size_t domain_end = baseUrl.find('/', pos);
            if (domain_end == std::string::npos)
                return baseUrl + "/" + url;
            
            std::string base_path = baseUrl.substr(domain_end);
            size_t last_slash = base_path.rfind('/');
            if (last_slash != std::string::npos) {
                base_path = base_path.substr(0, last_slash + 1);
            }
            return baseUrl.substr(0, domain_end) + base_path + url;
        }
    }
    
    throw PlaylistException("Invalid base URL: " + baseUrl);
}

uint64_t ParseXstreamInfTag(const std::string& data, std::string& url)
{
    constexpr auto bandwidth_tag = "BANDWIDTH=";
    size_t pos = data.find(bandwidth_tag);
    
    if (pos == std::string::npos)
        throw PlaylistException("Missing BANDWIDTH in EXT-X-STREAM-INF");
        
    pos += strlen(bandwidth_tag);
    uint64_t bandwidth = stoull(data.substr(pos));
    
    size_t url_start = data.find('\n', pos) + 1;
    url = data.substr(url_start);
    trim(url);
    
    return bandwidth;
}

bool IsPlaylistContent(const std::string& content)
{
    return content.find("#EXTM3U") != std::string::npos;
}

Playlist::Playlist(const std::string& urlOrContent, uint64_t indexOffset)
    : m_indexOffset(indexOffset),
      m_targetDuration(0),
      m_initialInternalIndex(-1)
{
    if (!IsPlaylistContent(urlOrContent)) {
        m_playListUrl = urlOrContent;
        size_t headers_pos = m_playListUrl.find('|');
        
        if (headers_pos != std::string::npos) {
            m_httplHeaders = m_playListUrl.substr(headers_pos);
            m_playListUrl = m_playListUrl.substr(0, headers_pos);
        }
        
        std::string data;
        LoadPlaylist(data);
        SetBestPlaylist(data);
    } else {
        SetBestPlaylist(urlOrContent);
    }
}

void Playlist::SetBestPlaylist(const std::string& data)
{
    constexpr auto stream_inf_tag = "#EXT-X-STREAM-INF:";
    size_t pos = data.find(stream_inf_tag);
    
    if (pos != std::string::npos) {
        uint64_t best_rate = 0;
        
        while (pos != std::string::npos) {
            pos += strlen(stream_inf_tag);
            size_t end_tag = data.find('#', pos);
            std::string tag_body = data.substr(pos, end_tag - pos);
            
            std::string url;
            uint64_t rate = ParseXstreamInfTag(tag_body, url);
            
            if (rate > best_rate) {
                m_playListUrl = ToAbsoluteUrl(url, m_effectivePlayListUrl);
                best_rate = rate;
            }
            
            pos = data.find(stream_inf_tag, end_tag);
        }
        
        std::string new_data;
        LoadPlaylist(new_data);
        ParsePlaylist(new_data);
    } else {
        ParsePlaylist(data);
    }
    
    if (!m_segmentUrls.empty()) {
        m_loadIterator = m_segmentUrls.begin()->first;
    }
}

bool Playlist::ParsePlaylist(const std::string& data)
{
    try {
        constexpr auto target_duration_tag = "#EXT-X-TARGETDURATION:";
        size_t pos = data.find(target_duration_tag);
        
        if (pos == std::string::npos)
            throw PlaylistException("Missing EXT-X-TARGETDURATION");
            
        pos += strlen(target_duration_tag);
        m_targetDuration = stoi(data.substr(pos));

        constexpr auto media_sequence_tag = "#EXT-X-MEDIA-SEQUENCE:";
        pos = data.find(media_sequence_tag);
        int64_t media_index = m_indexOffset;
        
        if (pos != std::string::npos) {
            pos += strlen(media_sequence_tag);
            uint64_t internal_index = stoull(data.substr(pos));
            
            if (m_initialInternalIndex == -1) {
                m_initialInternalIndex = internal_index;
            }
            
            media_index += internal_index - m_initialInternalIndex;
        }

        m_isVod = data.find("#EXT-X-ENDLIST") != std::string::npos;

        constexpr auto inf_tag = "#EXTINF:";
        pos = data.find(inf_tag);
        bool has_content = false;
        
        while (pos != std::string::npos) {
            pos += strlen(inf_tag);
            size_t comma_pos = data.find(',', pos);
            float duration = stof(data.substr(pos, comma_pos - pos));
            
            size_t url_start = data.find('\n', comma_pos) + 1;
            size_t url_end = data.find('\n', url_start);
            std::string url = data.substr(url_start, url_end - url_start);
            trim(url);
            
            url = ToAbsoluteUrl(url, m_effectivePlayListUrl) + m_httplHeaders;
            
            TimeOffset start_time = GetTimeOffset();
            
            if (!m_segmentUrls.empty()) {
                const auto& prev = m_segmentUrls.rbegin()->second;
                start_time = prev.startTime + prev.duration;
            }
            
            m_segmentUrls.emplace(media_index, 
                SegmentInfo{start_time, duration, url, media_index});
            
            ++media_index;
            pos = data.find(inf_tag, url_end);
            has_content = true;
        }
        
        return has_content;
    } catch (const std::exception& e) {
        kodi::Log(ADDON_LOG_ERROR, "Playlist parse error: %s", e.what());
        throw;
    }
}

void Playlist::LoadPlaylist(std::string& data) const
{
    try {
        kodi::vfs::CFile file;
        std::string url = m_effectivePlayListUrl.empty() ? 
            m_playListUrl : m_effectivePlayListUrl;
        
        if (!file.CURLCreate(url) || !file.CURLOpen(0)) {
            throw PlaylistException("Failed to open playlist URL");
        }
        
        char buffer[4096];
        ssize_t bytes_read;
        
        while ((bytes_read = file.Read(buffer, sizeof(buffer))) > 0) {
            data.append(buffer, bytes_read);
        }
        
        size_t headers_pos = url.find('|');
        if (headers_pos != std::string::npos) {
            m_effectivePlayListUrl = url.substr(0, headers_pos);
        }
    } catch (const std::exception& e) {
        kodi::Log(ADDON_LOG_ERROR, "Playlist load error: %s", e.what());
        throw PlaylistException("Playlist load failed");
    }
}

bool Playlist::Reload()
{
    if (m_isVod) return true;
    
    try {
        std::string data;
        LoadPlaylist(data);
        return ParsePlaylist(data);
    } catch (...) {
        return false;
    }
}

bool Playlist::NextSegment(SegmentInfo& info, bool& hasMore)
{
    auto it = m_segmentUrls.find(m_loadIterator);
    
    if (it != m_segmentUrls.end()) {
        info = it->second;
        hasMore = m_segmentUrls.count(++m_loadIterator) > 0;
        return true;
    }
    
    return false;
}

bool Playlist::SetNextSegmentIndex(uint64_t index)
{
    if (m_segmentUrls.count(index)) {
        m_loadIterator = index;
        return true;
    }
    return false;
}
}
