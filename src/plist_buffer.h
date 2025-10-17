/*
 *
 *   Copyright (C) 2019 Sergey Shramchenko
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

#ifndef __plist_buffer_delegate_h__
#define __plist_buffer_delegate_h__

#include <memory>
#include <ctime>

namespace Buffers
{
    class IPlaylistBufferDelegate
    {
    public:
        virtual ~IPlaylistBufferDelegate() = default;
        
        /**
         * @brief Get the number of segments to cache
         * @return Number of segments to keep in cache
         */
        virtual int SegmentsAmountToCache() const = 0;
        
        /**
         * @brief Get the total duration of the stream
         * @return Duration in seconds
         */
        virtual time_t Duration() const = 0;
        
        /**
         * @brief Get URL for timeshift playback
         * @param timeshift Timeshift value in seconds
         * @param timeshiftAdjusted Adjusted timeshift value (output parameter)
         * @return URL for timeshift playback
         */
        virtual std::string UrlForTimeshift(time_t timeshift, time_t* timeshiftAdjusted) const = 0;
        
        /**
         * @brief Check if stream is live
         * @return true if live stream, false for VOD
         */
        virtual bool IsLive() const = 0;
        
        /**
         * @brief Get current playback position
         * @return Current position in seconds
         */
        virtual time_t GetCurrentPosition() const = 0;
        
        /**
         * @brief Set current playback position
         * @param position New position in seconds
         */
        virtual void SetCurrentPosition(time_t position) = 0;
        
        /**
         * @brief Get minimum available timeshift
         * @return Minimum timeshift in seconds
         */
        virtual time_t GetMinTimeshift() const = 0;
        
        /**
         * @brief Get maximum available timeshift
         * @return Maximum timeshift in seconds
         */
        virtual time_t GetMaxTimeshift() const = 0;
    };
    
    typedef std::shared_ptr<IPlaylistBufferDelegate> PlaylistBufferDelegate;
}

#endif /* __plist_buffer_delegate_h__ */
