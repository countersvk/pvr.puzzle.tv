/*
 *      Copyright (C) 2017 Sergey Shramchenko
 *      https://github.com/srg70/pvr.puzzle.tv
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

#include <kodi/AddonBase.h>
#include <kodi/General.h>
#include <memory>
#include "globals.hpp"
#include "puzzle_pvr_client.h"
#include "ttv_pvr_client.h"
#include "sharatv_pvr_client.h"
#include "TimersEngine.hpp"

namespace {
    std::unique_ptr<Engines::TimersEngine> m_timersEngine;
    std::unique_ptr<IPvrtvDataSource> m_dataSource;
    int m_clientType = 1;
}

namespace Globals {
    void CreateWithHandle(IAddonDelegate* pvr);
    void Cleanup();
}

class ATTRIBUTE_HIDDEN PVRPuzzleTv final
  : public kodi::addon::CAddonBase,
    public kodi::addon::CInstancePVRClient,
    public IAddonDelegate
{
public:
    // region IAddonDelegate implementation
    void Addon_TriggerRecordingUpdate() override { TriggerRecordingUpdate(); }
    void Addon_AddMenuHook(const kodi::addon::PVRMenuhook& hook) override { AddMenuHook(hook); }
    void Addon_TriggerChannelUpdate() override { TriggerChannelUpdate(); }
    void Addon_TriggerChannelGroupsUpdate() override { TriggerChannelGroupsUpdate(); }
    void Addon_TriggerEpgUpdate(unsigned int channelUid) override { TriggerEpgUpdate(channelUid); }
    void Addon_TriggerTimerUpdate() override { TriggerTimerUpdate(); }
    // endregion

    ADDON_STATUS Create() override
    {
        Globals::CreateWithHandle(this);
        m_clientType = kodi::GetSettingInt("provider_type");
        
        m_dataSource = CreateDataSource();
        if (!m_dataSource) {
            kodi::QueueFormattedNotification(QUEUE_ERROR, kodi::GetLocalizedString(32001).c_str());
            return ADDON_STATUS_NEED_SETTINGS;
        }
        
        const ADDON_STATUS result = m_dataSource->Init(kodi::GetBaseUserPath(), kodi::GetAddonPath());
        m_timersEngine = std::make_unique<Engines::TimersEngine>(m_dataSource.get());
        return result;
    }

    ADDON_STATUS SetSetting(const std::string& settingName, const kodi::CSettingValue& settingValue) override
    {
        if (settingName == "provider_type") {
            const int newValue = settingValue.GetInt();
            if(m_clientType != newValue) {
                m_clientType = newValue;
                return ADDON_STATUS_NEED_RESTART;
            }
            return ADDON_STATUS_OK;
        }
        return m_dataSource->SetSetting(settingName, settingValue);
    }

    // region PVR Client implementation
    PVR_ERROR GetCapabilities(kodi::addon::PVRCapabilities& capabilities) override
    {
        return m_dataSource->GetAddonCapabilities(capabilities);
    }
    
    PVR_ERROR GetBackendName(std::string& name) override
    {
        name = "Puzzle TV PVR Add-on";
        return PVR_ERROR_NO_ERROR;
    }
    
    PVR_ERROR GetBackendVersion(std::string& version) override
    {
        version = STR(IPTV_VERSION);
        return PVR_ERROR_NO_ERROR;
    }
    
    PVR_ERROR GetEPGForChannel(int channelUid, time_t start, time_t end, 
                              kodi::addon::PVREPGTagsResultSet& results) override
    {
        return m_dataSource->GetEPGForChannel(channelUid, start, end, results);
    }
    
    PVR_ERROR GetChannels(bool radio, kodi::addon::PVRChannelsResultSet& results) override
    {
        return m_dataSource->GetChannels(radio, results);
    }
    
    PVR_ERROR GetChannelGroups(bool radio, kodi::addon::PVRChannelGroupsResultSet& results) override
    {
        return m_dataSource->GetChannelGroups(radio, results);
    }
    
    PVR_ERROR GetChannelGroupMembers(const kodi::addon::PVRChannelGroup& group, 
                                    kodi::addon::PVRChannelGroupMembersResultSet& results) override
    {
        return m_dataSource->GetChannelGroupMembers(group, results);
    }
    
    bool OpenLiveStream(const kodi::addon::PVRChannel& channel) override
    {
        return m_dataSource->OpenLiveStream(channel);
    }
    
    int ReadLiveStream(unsigned char* buffer, unsigned int size) override
    {
        return m_dataSource->ReadLiveStream(buffer, size);
    }
    
    PVR_ERROR AddTimer(const kodi::addon::PVRTimer& timer) override
    {
        return m_timersEngine ? m_timersEngine->AddTimer(timer) : PVR_ERROR_FAILED;
    }
    
    PVR_ERROR GetTimers(kodi::addon::PVRTimersResultSet& results) override
    {
        return m_timersEngine ? m_timersEngine->GetTimers(results) : PVR_ERROR_FAILED;
    }
    // endregion

private:
    std::unique_ptr<IPvrtvDataSource> CreateDataSource()
    {
        switch (m_clientType) {
            case 0: return std::make_unique<PuzzlePVRClient>();
            case 4: return std::make_unique<TtvPVRClient>();
            case 5: return std::make_unique<SharaTvPVRClient>();
            default: return nullptr;
        }
    }
};

ADDONCREATOR(PVRPuzzleTv)
