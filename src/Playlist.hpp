#ifndef PLAYLIST_HPP
#define PLAYLIST_HPP

#include <kodi/AddonBase.h>
#include <chrono>
#include <string>
#include <map>
#include <exception>
#include <memory>
#include <utility>

namespace Buffers {

using TimeOffset = float;
using Timestamp = std::chrono::system_clock::time_point;

struct SegmentInfo {
    SegmentInfo() = default;
    SegmentInfo(TimeOffset start, float dur, std::string url, uint64_t idx)
        : startTime(start), duration(dur), url(std::move(url)), index(idx) {}

    std::string url;
    TimeOffset startTime = 0.0f;
    float duration = 0.0f;
    uint64_t index = 0;

    SegmentInfo(SegmentInfo&&) noexcept = default;
    SegmentInfo& operator=(SegmentInfo&&) noexcept = default;
};

class PlaylistException : public std::exception {
public:
    explicit PlaylistException(std::string reason)
        : m_reason(std::move(reason)) {}
    
    const char* what() const noexcept override { 
        return m_reason.c_str(); 
    }

private:
    std::string m_reason;
};

class Playlist {
public:
    explicit Playlist(const std::string& url, uint64_t indexOffset = 0);
    
    // Запрещаем копирование
    Playlist(const Playlist&) = delete;
    Playlist& operator=(const Playlist&) = delete;
    
    // Поддержка перемещения
    Playlist(Playlist&&) noexcept = default;
    Playlist& operator=(Playlist&&) noexcept = default;

    bool NextSegment(SegmentInfo& info, bool& hasMoreSegments) noexcept;
    bool SetNextSegmentIndex(uint64_t offset) noexcept;
    bool Reload();
    
    bool IsVod() const noexcept { return m_isVod; }
    int TargetDuration() const noexcept { return m_targetDuration; }
    TimeOffset GetTimeOffset() const noexcept { 
        return m_targetDuration * m_indexOffset; 
    }

private:
    using SegmentMap = std::map<uint64_t, SegmentInfo>;
    
    void ParsePlaylist(const std::string& data);
    void SetBestPlaylist(const std::string& playlistUrl);
    void LoadPlaylist(std::string& data) const;

    // Члены класса
    SegmentMap m_segmentUrls;
    std::string m_playListUrl;
    mutable std::string m_effectivePlayListUrl;
    std::string m_httpHeaders;
    
    uint64_t m_loadIterator = 0;
    uint64_t m_indexOffset = 0;
    int m_targetDuration = 0;
    bool m_isVod = false;
};

} // namespace Buffers

#endif // PLAYLIST_HPP
