/*
 *      Copyright (C) 2017 Sergey Shramchenko
 *      https://github.com/srg70/pvr.puzzle.tv
 *
 *      Copyright (C) 2013-2015 Anton Fedchin
 *      http://github.com/afedchin/xbmc-addon-iptvsimple/
 *
 *      Copyright (C) 2011 Pulse-Eight
 *      http://www.pulse-eight.com/
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
 */

#ifndef PVR_PUZZLE_TV_INTERFACES_H
#define PVR_PUZZLE_TV_INTERFACES_H

#include <kodi/addon-instance/PVR.h>
#include <cstddef>
#include <memory>

class ITimersEngineDelegate {
public:
    virtual bool StartRecordingFor(kodi::addon::PVRTimer& timer) = 0;
    virtual bool StopRecordingFor(kodi::addon::PVRTimer& timer) = 0;
    virtual bool FindEpgFor(kodi::addon::PVRTimer& timer) = 0;
    
    virtual ~ITimersEngineDelegate() = default;
};

class IAddonDelegate {
public:
    virtual void Addon_TriggerRecordingUpdate() = 0;
    virtual void Addon_AddMenuHook(const kodi::addon::PVRMenuhook& hook) = 0;
    virtual void Addon_TriggerChannelUpdate() = 0;
    virtual void Addon_TriggerChannelGroupsUpdate() = 0;
    virtual void Addon_TriggerEpgUpdate(unsigned int channelUid) = 0;
    virtual void Addon_TriggerTimerUpdate() = 0;

    virtual ~IAddonDelegate() = default;
};

class IPvrIptvDataSource : public ITimersEngineDelegate {
public:
    [[nodiscard]] virtual ADDON_STATUS Init(const std::string& clientPath, 
                                           const std::string& userPath) = 0;
    [[nodiscard]] virtual ADDON_STATUS GetStatus() const noexcept = 0;

    virtual ADDON_STATUS SetSetting(const std::string& settingName, 
                                   const kodi::CSettingValue& settingValue) = 0;

    [[nodiscard]] virtual PVR_ERROR GetAddonCapabilities(
        kodi::addon::PVRCapabilities& capabilities) const noexcept = 0;
    
    [[nodiscard]] virtual PVR_ERROR GetEPGForChannel(int channelUid, 
                                                    time_t start, 
                                                    time_t end, 
                          kodi::addon::PVREPGTagsResultSet& results) const = 0;
    
    [[nodiscard]] virtual int GetChannelsAmount() const noexcept = 0;
    [[nodiscard]] virtual PVR_ERROR GetChannels(
        bool radio, 
        kodi::addon::PVRChannelsResultSet& results) const = 0;
    
    [[nodiscard]] virtual bool OpenLiveStream(
        const kodi::addon::PVRChannel& channel) = 0;
    
    virtual void CloseLiveStream() noexcept = 0;
    
    [[nodiscard]] virtual int GetChannelGroupsAmount() const noexcept = 0;
    [[nodiscard]] virtual PVR_ERROR GetChannelGroups(
        bool radio, 
        kodi::addon::PVRChannelGroupsResultSet& results) const = 0;
    
    [[nodiscard]] virtual PVR_ERROR GetChannelGroupMembers(
        const kodi::addon::PVRChannelGroup& group, 
        kodi::addon::PVRChannelGroupMembersResultSet& results) const = 0;

    [[nodiscard]] virtual bool CanPauseStream() const noexcept = 0;
    [[nodiscard]] virtual bool CanSeekStream() const noexcept = 0;
    [[nodiscard]] virtual bool IsRealTimeStream() const noexcept = 0;
    
    [[nodiscard]] virtual PVR_ERROR GetStreamTimes(
        kodi::addon::PVRStreamTimes& times) const noexcept = 0;
    
    [[nodiscard]] virtual int64_t SeekLiveStream(int64_t position, 
                                                int whence) noexcept = 0;
    [[nodiscard]] virtual int ReadLiveStream(
        std::byte* buffer, 
        std::size_t size) noexcept = 0;

    // Остальные методы интерфейса с аналогичными модификаторами
    // ...

    virtual ~IPvrtvDataSource() override = default;
};

class ITimersEngine {
public:
    [[nodiscard]] virtual int GetTimersAmount() const noexcept = 0;
    [[nodiscard]] virtual PVR_ERROR AddTimer(
        const kodi::addon::PVRTimer& timer) noexcept = 0;
    [[nodiscard]] virtual PVR_ERROR GetTimers(
        kodi::addon::PVRTimersResultSet& results) const noexcept = 0;
    [[nodiscard]] virtual PVR_ERROR DeleteTimer(
        const kodi::addon::PVRTimer& timer, 
        bool forceDelete) noexcept = 0;
    [[nodiscard]] virtual PVR_ERROR UpdateTimer(
        const kodi::addon::PVRTimer& timer) noexcept = 0;

    virtual ~ITimersEngine() = default;
};

#endif // PVR_PUZZLE_TV_INTERFACES_H
