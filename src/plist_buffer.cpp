/*
 *
 *   Copyright (C) 2017 Sergey Shramchenko
 *   https://github.com/srg70/pvr.puzzle.tv
 *
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with XBMC; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *  http://www.gnu.org/copyleft/gpl.html
 *
 */

#include <chrono>
#include <thread>
#include <atomic>
#include <memory>
#include "ThreadPool.h"
#include "helpers.h"
#include "plist_buffer.h"
#include "globals.hpp"
#include "playlist_cache.hpp"
#include "kodi/addon-instance/Inputstream.h"
#include "kodi/Filesystem.h"
#include "kodi/General.h"

using namespace Globals;
using namespace Helpers;

namespace Buffers {
    int PlaylistBuffer::s_numberOfHlsThreads = 1;
    
    int PlaylistBuffer::SetNumberOfHlsThreads(int numOfThreads) {
        const auto numOfCpu = std::thread::hardware_concurrency();
        if(numOfThreads < 0)
            numOfThreads = 1;
        else if(numOfThreads > numOfCpu)
            numOfThreads = numOfCpu;
        return s_numberOfHlsThreads = numOfThreads;
    }

    PlaylistBuffer::PlaylistBuffer(const std::string &playListUrl, PlaylistBufferDelegate delegate, bool seekForVod)
    : m_delegate(delegate)
    , m_cache(nullptr)
    , m_url(playListUrl)
    , m_seekForVod(seekForVod)
    , m_isWaitingForRead(false)
    , m_stopped(false)
    , m_position(0)
    , m_currentSegment(nullptr)
    , m_segmentIndexAfterSeek(0)
    {
        Init(playListUrl);
    }
    
    PlaylistBuffer::~PlaylistBuffer()
    {
        StopThread();
        if(m_cache)
            delete m_cache;
    }
    
    void PlaylistBuffer::CreateThread()
    {
        m_stopped = false;
        m_thread = std::thread(&PlaylistBuffer::Process, this);
    }
    
    void PlaylistBuffer::Init(const std::string &playlistUrl)
    {
        StopThread(20000);
        {
            std::lock_guard<std::mutex> lock(m_syncAccess);

            if(m_cache)
                delete m_cache;
            try {
                m_cache = new PlaylistCache(playlistUrl, m_delegate, m_seekForVod);
            } catch (PlaylistException& ex) {
                kodi::Log(ADDON_LOG_ERROR, "Playlist exception: %s", ex.what());
                throw PlistBufferException((std::string("Playlist exception: ") + ex.what()).c_str());
            }
            m_position = 0;
            m_currentSegment = nullptr;
            m_segmentIndexAfterSeek = 0;
        }
        CreateThread();
        m_cache->WaitForBitrate();
    }
        
    static bool FillSegmentFromPlaylist(MutableSegment* segment, const std::string& content, std::function<bool(const MutableSegment&)> IsCanceled)
    {
        Playlist plist(content);

        bool hasMoreSegments = false;
        bool isCanceled = false;
        SegmentInfo info;
        while(plist.NextSegment(info, hasMoreSegments)) {
            kodi::vfs::CFile f;
            if(!f.OpenFile(info.url, ADDON_READ_NO_CACHE | ADDON_READ_CHUNKED))
                throw PlistBufferException("Failed to open media segment of sub-playlist.");

            unsigned char buffer[8196];
            ssize_t bytesRead;
            do {
                bytesRead = f.Read(buffer, sizeof(buffer));
                segment->Push(buffer, bytesRead);
                isCanceled = IsCanceled(*segment);
            }while (bytesRead > 0 && !isCanceled);
            
            f.Close();
            
            if(!hasMoreSegments || isCanceled)
                break;
        }
        
        if(isCanceled){
             LogDebug("PlaylistBuffer: segment #%" PRIu64 " CANCELED.", segment->info.index);
             return false;
         } else if(segment->BytesReady() == 0) {
             LogDebug("PlaylistBuffer: segment #%" PRIu64 " FAILED.", segment->info.index);
             return false;
         }

         LogDebug("PlaylistBuffer: segment #%" PRIu64 " FINISHED.", segment->info.index);
         return true;
    }

    static bool FillSegment(MutableSegment* segment, std::function<bool(const MutableSegment&)> IsCanceled, std::function<void(bool,MutableSegment*)> segmentDone)
    {
        std::hash<std::thread::id> hasher;
        LogDebug("PlaylistBuffer: segment #%" PRIu64 " STARTED. (thread 0x%X).", segment->info.index, hasher(std::this_thread::get_id()));

        bool isCanceled = IsCanceled(*segment);
        bool result = !isCanceled;

        do {
            // Do not bother the server with canceled segments
            if(isCanceled)
                break;
            
            kodi::vfs::CFile f;
            if(!f.OpenFile(segment->info.url, ADDON_READ_NO_CACHE | ADDON_READ_CHUNKED | ADDON_READ_TRUNCATED))
                throw PlistBufferException("Failed to download playlist media segment.");
            
            // Some content type should be treated as playlist
            auto contentType = f.GetPropertyValue(ADDON_FILE_PROPERTY_CONTENT_TYPE, "");
            const bool contentIsPlaylist =  "application/vnd.apple.mpegurl" == contentType  || "audio/mpegurl" == contentType;
            
            unsigned char buffer[8196];
            std::string contentForPlaylist;
            ssize_t bytesRead;
            do {
                bytesRead = f.Read(buffer, sizeof(buffer));
                if(contentIsPlaylist) {
                    contentForPlaylist.append((char *) buffer, bytesRead);
                } else{
                    segment->Push(buffer, bytesRead);
                }
                isCanceled = IsCanceled(*segment);
            }while (bytesRead > 0 && !isCanceled);
            
            f.Close();
            
            if(contentIsPlaylist && !isCanceled) {
                result = FillSegmentFromPlaylist(segment, contentForPlaylist, [&isCanceled, &IsCanceled](const MutableSegment& seg){
                    return isCanceled = IsCanceled(seg);
                });
            }
            
        } while(false);
        
        if(isCanceled){
            LogDebug("PlaylistBuffer: segment #%" PRIu64 " CANCELED.", segment->info.index);
            result = false;
        } else if(segment->BytesReady() == 0) {
            LogDebug("PlaylistBuffer: segment #%" PRIu64 " FAILED.", segment->info.index);
            result = false;
        } else {
            LogDebug("PlaylistBuffer: segment #%" PRIu64 " FINISHED.", segment->info.index);
        }
        segmentDone(result, segment);

        return result;
    }
    
    bool PlaylistBuffer::IsStopped(uint32_t timeoutInSec) {
        auto timeout = std::chrono::milliseconds(timeoutInSec * 1000);
        auto start = std::chrono::steady_clock::now();
        
        do{
            if (m_stopped) return true;
            
            auto elapsed = std::chrono::steady_clock::now() - start;
            if (elapsed >= timeout) return m_stopped;
            
            std::this_thread::sleep_for(std::chrono::seconds(1));
        } while(true);
        
        return m_stopped;
    }
    
    void PlaylistBuffer::Process()
    {
        using namespace progschj;

        ThreadPool pool(s_numberOfHlsThreads);
        pool.set_queue_size_limit(s_numberOfHlsThreads);

        try {
            while (!m_stopped) {
                
                bool cacheIsFull = false;
                MutableSegment* segment = nullptr;
                uint64_t segmentIdx(-1);
                {
                    std::lock_guard<std::mutex> lock(m_syncAccess);
                    segment = m_cache->SegmentToFill();
                    
                    if(nullptr != segment) {
                        segmentIdx = segment->info.index;
                        LogDebug("PlaylistBuffer: segment #%" PRIu64 " INITIALIZED.", segmentIdx);
                    }
                    cacheIsFull = !m_cache->HasSpaceForNewSegment(segmentIdx);
                }

                const uint64_t segmentIndexAfterSeek = m_segmentIndexAfterSeek;
                std::function<bool(const MutableSegment&)> isSegmentCanceled = [this, segmentIndexAfterSeek](const MutableSegment& seg) {
                    return m_stopped || (m_segmentIndexAfterSeek != segmentIndexAfterSeek && seg.info.index != m_segmentIndexAfterSeek);
                };

                // Wait for cache space if needed
                while(cacheIsFull && !m_stopped){
                    {
                        std::lock_guard<std::mutex> lock(m_syncAccess);
                        cacheIsFull = !m_cache->HasSpaceForNewSegment(segmentIdx);
                    }
                    
                    if(cacheIsFull) {
                        if(nullptr != segment && isSegmentCanceled(*segment))
                            break;
                            
                        if(IsStopped(1))
                            break;
                            
                        LogDebug("PlaylistBuffer: waiting for space in cache...");
                        if(nullptr != segment) {
                            // Ping server to avoid connection timeout
                            kodi::vfs::FileStatus stat;
                            kodi::vfs::StatFile(segment->info.url, stat);
                        }
                    }
                };
                
                if(segment && !m_stopped) {
                    // Load segment data
                    auto startLoadingAt = std::chrono::system_clock::now();
                    std::function<void(bool,MutableSegment*)> segmentDone = [this, startLoadingAt](bool segmentReady, MutableSegment* seg) {
                        if(!m_stopped){
                            std::lock_guard<std::mutex> lock(m_syncAccess);
                            if(segmentReady) {
                                m_cache->SegmentReady(seg);
                                m_writeEvent.notify_all();
                                auto endLoadingAt = std::chrono::system_clock::now();
                                std::chrono::duration<float> loadTime = endLoadingAt - startLoadingAt;
                                LogDebug("PlaylistBuffer: segment #%" PRIu64 " loaded in %0.2f sec. Duration %0.2f", seg->info.index, loadTime.count(), seg->Duration());
                            } else {
                                m_cache->SegmentCanceled(seg);
                            }
                        }
                    };

                    pool.enqueue(FillSegment, segment, isSegmentCanceled, segmentDone);
                } else {
                    IsStopped(1);
                }

                // Update playlist regularly
                if(!m_stopped)
                {
                    std::lock_guard<std::mutex> lock(m_syncAccess);
                    if(!m_cache->ReloadPlaylist()) {
                        LogError("PlaylistBuffer: playlist update failed.");
                        break;
                    }
                }
            }
            
        } catch (InputBufferException& ex) {
            LogError("PlaylistBuffer: download thread failed with error: %s", ex.what());
        }

        LogDebug("PlaylistBuffer: finalizing loaders pool...");
        pool.wait_until_empty();
        pool.wait_until_nothing_in_flight();

        LogDebug("PlaylistBuffer: write thread is done.");
    }
    
    ssize_t PlaylistBuffer::Read(unsigned char *buffer, size_t bufferSize, uint32_t timeoutMs)
    {
        if(m_stopped) {
            LogError("PlaylistBuffer: write thread is not running.");
            return -1;
        }
        m_isWaitingForRead = true;
        
        size_t totalBytesRead = 0;
        bool isEof = false;
        PlaylistCache::SegmentStatus segmentStatus;
        
        while (totalBytesRead < bufferSize && !m_stopped)
        {
            while(nullptr == m_currentSegment)
            {
                {
                    std::lock_guard<std::mutex> lock(m_syncAccess);
                    m_currentSegment = m_cache->NextSegment(segmentStatus);
                }
                
                if(nullptr == m_currentSegment) {
                    if((isEof = PlaylistCache::k_SegmentStatus_EOF == segmentStatus)) {
                        LogNotice("PlaylistBuffer: EOF reported.");
                        break;
                    }
                    if(PlaylistCache::k_SegmentStatus_Loading == segmentStatus ||
                       PlaylistCache::k_SegmentStatus_CacheEmpty == segmentStatus)
                    {
                        if(m_stopped){
                            LogDebug("PlaylistBuffer: stopping...");
                            break;
                        }
                        
                        LogDebug("PlaylistBuffer: waiting for segment loading (max %d ms)...", timeoutMs);
                        std::unique_lock<std::mutex> lock(m_syncAccess);
                        auto startAt = std::chrono::system_clock::now();
                        m_writeEvent.wait_for(lock, std::chrono::milliseconds(timeoutMs));
                        auto waitingMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::system_clock::now() - startAt);
                        timeoutMs -= waitingMs.count();
                        
                        if(timeoutMs < 1000)
                        {
                            LogError("PlaylistBuffer: segment loading timeout!");
                            break;
                        }
                    } else {
                        LogError("PlaylistBuffer: segment not found. Reason %d.", segmentStatus);
                        break;
                    }
                }
            }

            if(nullptr == m_currentSegment || m_stopped)
            {
                LogDebug("PlaylistBuffer: no segment for read.");
                break;
            }
            
            size_t bytesToRead = bufferSize - totalBytesRead;
            size_t bytesRead;
            do {
                bytesRead = m_currentSegment->Read(buffer + totalBytesRead, bytesToRead);
                totalBytesRead += bytesRead;
                m_position += bytesRead;
                bytesToRead = bufferSize - totalBytesRead;
            } while(bytesToRead > 0 && bytesRead > 0);
            
            if(m_currentSegment->BytesReady() <= 0) {
                LogDebug("PlaylistBuffer: read all data from segment. Moving next...");
                m_currentSegment = nullptr;
            }
        }
        
        m_isWaitingForRead = false;
        return (!isEof && !m_stopped) ? totalBytesRead : -1;
    }
    
    void PlaylistBuffer::AbortRead(){
        StopThread();
        while(m_isWaitingForRead) {
            LogDebug("PlaylistBuffer: waiting for reading abort 100 ms...");
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            m_writeEvent.notify_all();
        }
    }

    bool PlaylistBuffer::SwitchStream(const std::string &newUrl)
    {
        bool succeeded = false;
        try {
            Init(newUrl);
            succeeded = true;
        } catch (const InputBufferException& ex) {
            kodi::Log(ADDON_LOG_ERROR, "PlaylistBuffer: Failed to switch streams to %s. Error: %s", newUrl.c_str(), ex.what());
        }
        
        return succeeded;
    }
    
    int64_t PlaylistBuffer::GetLength() const
    {
        return m_cache->Length();
    }
    
    int64_t PlaylistBuffer::GetPosition() const
    {
        if(!m_cache->CanSeek()) {
            LogDebug("PlaylistBuffer: Plist archive position -1");
            return -1;
        }
        LogDebug("PlaylistBuffer: Plist archive position %" PRId64 "", m_position);
        return m_position;
    }
    
    int64_t PlaylistBuffer::Seek(int64_t iPosition, int iWhence)
    {
        if(!m_cache->CanSeek())
            return -1;

        LogDebug("PlaylistBuffer: Seek requested pos %" PRId64 ", from %d", iPosition, iWhence);

        // Translate position to offset from start of buffer.
        int64_t length = GetLength();
        int64_t begin = 0;

        if(iWhence == SEEK_CUR) {
            iPosition = m_position + iPosition;
        } else if(iWhence == SEEK_END) {
            iPosition = length + iPosition;
        }
        
        if(iPosition < 0 ) {
            LogDebug("PlaylistBuffer: Seek can't be pos %" PRId64 "", iPosition);
            iPosition = 0;
        }

        if(iPosition > length) {
            iPosition = length;
        }
        if(iPosition < begin) {
            iPosition = begin;
        }

        LogDebug("PlaylistBuffer: Seek calculated pos %" PRId64 "", iPosition);

        if(iPosition == m_position)
            return m_position;

        {
            std::lock_guard<std::mutex> lock(m_syncAccess);
            uint64_t nextSegmentIndex;
            if(!m_cache->PrepareSegmentForPosition(iPosition, &nextSegmentIndex)) {
                LogDebug("PlaylistBuffer: cache failed to prepare for seek to pos %" PRId64 "", iPosition);
                return -1;
            }
            m_segmentIndexAfterSeek = nextSegmentIndex;
        }
        
        m_currentSegment = nullptr;
        m_position = iPosition;
        return m_position;
    }
    
    bool PlaylistBuffer::StopThread(int iWaitMs)
    {
        LogDebug("PlaylistBuffer: terminating loading thread...");
        m_stopped = true;
        m_writeEvent.notify_all();
        
        if(m_thread.joinable()) {
            if(iWaitMs <= 0) {
                m_thread.detach();
            } else {
                m_thread.join();
            }
        }
        
        return true;
    }
}
