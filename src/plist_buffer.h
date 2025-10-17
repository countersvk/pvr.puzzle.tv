/*
 *
 *   Copyright (C) 2017 Sergey Shramchenko
 *   https://github.com/srg70/pvr.puzzle.tv
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

#ifndef plist_buffer_h
#define plist_buffer_h

#include <string>
#include <vector>
#include <list>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>

#include "kodi/addon-instance/Inputstream.h"
#include "kodi/addon-instance/PVR.h"
#include "input_buffer.h"
#include "plist_buffer_delegate.h"

namespace Buffers
{
    class Segment;
    class MutableSegment;
    class PlaylistCache;
    
    class PlaylistBuffer : public InputBuffer
    {
    public:
        PlaylistBuffer(const std::string &streamUrl, PlaylistBufferDelegate delegate, bool seekForVod);
        ~PlaylistBuffer();
        
        const std::string& GetUrl() const { return m_url; };
        int64_t GetLength() const override;
        int64_t GetPosition() const override;
        int64_t Seek(int64_t iPosition, int iWhence) override;
        ssize_t Read(unsigned char *buffer, size_t bufferSize, uint32_t timeoutMs) override;
        bool SwitchStream(const std::string &newUrl);
        void AbortRead() override;
        static int SetNumberOfHlsThreads(int numOfThreads);
        
        /*!
         * @brief Stop the thread
         * @param iWaitMs negative = don't wait, 0 = infinite, or the amount of ms to wait
         */
        virtual bool StopThread(int iWaitMs = 5000);
        
    private:
        mutable std::mutex m_syncAccess;
        mutable std::condition_variable m_writeEvent;
        PlaylistBufferDelegate m_delegate;
        int64_t m_position;
        PlaylistCache* m_cache;
        Segment* m_currentSegment;
        uint64_t m_segmentIndexAfterSeek;
        std::string m_url;
        const bool m_seekForVod;
        static int s_numberOfHlsThreads;
        bool m_isWaitingForRead;
        bool m_stopped;
        std::thread m_thread;

        void Process();
        void Init(const std::string &playlistUrl);
        bool IsStopped(uint32_t timeoutInSec = 0);
        void CreateThread();
    };
    
    class PlistBufferException : public InputBufferException
    {
    public:
        PlistBufferException(const char* reason = "")
        : m_reason(reason)
        , InputBufferException(NULL)
        {}
        virtual const char* what() const noexcept {return m_reason.c_str();}
        
    private:
        std::string m_reason;
    };
    
}
#endif //plist_buffer_h
