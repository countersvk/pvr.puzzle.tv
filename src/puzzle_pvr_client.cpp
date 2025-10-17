/*
 *
 *   Copyright (C) 2017 Sergey Shramchenko
 *   https://github.com/srg70/pvr.puzzle.tv
 *
 *  Copyright (C) 2013 Alex Deryskyba (alex@codesnake.com)
 *  https://bitbucket.org/codesnake/pvr.sovok.tv_xbmc_addon
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

#include <ctime>
#include <memory>
#include "kodi/General.h"
#include "kodi/gui/dialogs/Select.h"

#include "timeshift_buffer.h"
#include "direct_buffer.h"
#include "puzzle_pvr_client.h"
#include "helpers.h"
#include "puzzle_tv.h"
#include "globals.hpp"

using namespace Globals;
using namespace std;
using namespace PuzzleEngine;
using namespace PvrClient;
using namespace Helpers;

static const char* c_server_url_setting = "puzzle_server_uri";
static const char* c_server_port_setting = "puzzle_server_port";
static const char* c_server_retries_setting = "puzzle_server_retries";
static const char* c_epg_provider_setting = "puzzle_server_epg_provider_type";
static const char* c_epg_url_setting = "puzzle_server_epg_url";
static const char* c_epg_port_setting = "puzzle_server_epg_port";
static const char* c_server_version_setting = "puzzle_server_version";
static const char* c_seek_archives = "puzzle_seek_archives";
static const char* c_block_dead_streams = "puzzle_block_dead_streams";

const unsigned int UPDATE_CHANNEL_STREAMS_MENU_HOOK = PVRClientBase::s_lastCommonMenuHookId + 1;
const unsigned int UPDATE_CHANNELS_MENU_HOOK = UPDATE_CHANNEL_STREAMS_MENU_HOOK + 1;

ADDON_STATUS PuzzlePVRClient::Init(const std::string& clientPath, const std::string& userPath)
{
    ADDON_STATUS retVal = PVRClientBase::Init(clientPath, userPath);
    if(ADDON_STATUS_OK != retVal)
       return retVal;
    
    m_currentChannelStreamIdx = -1;
    
    m_serverPort = kodi::GetSettingInt(c_server_port_setting, 8089);
    m_serverUri = kodi::GetSettingString(c_server_url_setting);
    m_maxServerRetries = kodi::GetSettingInt(c_server_retries_setting, 4);

    m_epgUrl = kodi::GetSettingString(c_epg_url_setting);
    m_epgType = kodi::GetSettingEnum<EpgType>(c_epg_provider_setting, c_EpgType_File);
    
    if(m_epgType != c_EpgType_Server){
        m_epgType = c_EpgType_File;
    }
    
    m_epgPort = kodi::GetSettingInt(c_epg_port_setting, 8085);
    m_serverVersion = kodi::GetSettingEnum<ServerVersion>(c_server_version_setting, c_PuzzleServer3);
    
    bool supportSeek = kodi::GetSettingBoolean(c_seek_archives,false);
    SetSeekSupported(supportSeek);
    
    m_blockDeadStreams = kodi::GetSettingBoolean(c_block_dead_streams, true);
    
    kodi::addon::PVRMenuhook menuHook1(UPDATE_CHANNEL_STREAMS_MENU_HOOK, 32052, PVR_MENUHOOK_CHANNEL);
    kodi::addon::PVRMenuhook menuHook2(UPDATE_CHANNELS_MENU_HOOK, 32053, PVR_MENUHOOK_CHANNEL);
    
    PVR->AddMenuHook(menuHook1);
    PVR->AddMenuHook(menuHook2);

    retVal = CreateCoreSafe(false);
    
    return retVal;
}

void PuzzlePVRClient::PopulateSettings(AddonSettingsMutableDictionary& settings)
{
    // Implementation if needed
}

PuzzlePVRClient::~PuzzlePVRClient()
{
    CloseLiveStream();
    CloseRecordedStream();
    DestroyCoreSafe();
}

ADDON_STATUS PuzzlePVRClient::CreateCoreSafe(bool clearEpgCache)
{
    ADDON_STATUS retVal = ADDON_STATUS_OK;
    try
    {
        CreateCore(clearEpgCache);
        OnCoreCreated();
    }
    catch (std::exception& ex)
    {
        kodi::Log(ADDON_LOG_ERROR, "PuzzlePVRClient: Can't create Puzzle Server core. Exception: %s", ex.what());
        retVal = ADDON_STATUS_LOST_CONNECTION;
    }
    catch(...)
    {
        kodi::Log(ADDON_LOG_ERROR, "Puzzle Server: unhandled exception on reload EPG.");
        retVal = ADDON_STATUS_PERMANENT_FAILURE;
    }
    return retVal;
}

void PuzzlePVRClient::DestroyCoreSafe()
{
    if(m_puzzleTV != nullptr) {
        m_clientCore = nullptr;
        delete m_puzzleTV;
        m_puzzleTV = nullptr;
    }
}

void PuzzlePVRClient::CreateCore(bool clearEpgCache)
{
    DestroyCoreSafe();
    
    m_clientCore = m_puzzleTV = new PuzzleTV((ServerVersion) m_serverVersion, m_serverUri.c_str(), m_serverPort);
    m_puzzleTV->SetMaxServerRetries(m_maxServerRetries);
    m_puzzleTV->SetEpgParams(EpgType(m_epgType), m_epgUrl, m_epgPort);
    m_puzzleTV->IncludeCurrentEpgToArchive(HowToAddCurrentEpgToArchive());
    m_puzzleTV->SetEpgCorrectionShift(EpgCorrectionShift());
    m_puzzleTV->SetLocalLogosFolder(LocalLogosFolder());
    m_puzzleTV->InitAsync(clearEpgCache, IsArchiveSupported());
}

ADDON_STATUS PuzzlePVRClient::SetSetting(const std::string& settingName, const kodi::addon::CSettingValue& settingValue)
{
    ADDON_STATUS result = ADDON_STATUS_OK;

    if (c_server_port_setting == settingName ||
        c_server_url_setting == settingName ||
        c_server_retries_setting == settingName ||
        c_epg_url_setting == settingName ||
        c_epg_provider_setting == settingName ||
        c_server_version_setting == settingName ||
        c_epg_port_setting == settingName)
    {
        result = ADDON_STATUS_NEED_RESTART;
    }
    else if(c_seek_archives == settingName) {
        SetSeekSupported(settingValue.GetBoolean());
        result = ADDON_STATUS_NEED_RESTART;
    }
    else if(c_block_dead_streams == settingName) {
        m_blockDeadStreams = settingValue.GetBoolean();
    }
    else {
        result = PVRClientBase::SetSetting(settingName, settingValue);
    }
    return result;
}

PVR_ERROR PuzzlePVRClient::GetAddonCapabilities(kodi::addon::PVRCapabilities& capabilities)
{
    capabilities.SetSupportsEPG(true);
    capabilities.SetSupportsTV(true);
    capabilities.SetSupportsRadio(true);
    capabilities.SetSupportsChannelGroups(true);
    capabilities.SetHandlesInputStream(true);

    capabilities.SetSupportsTimers(false);
    capabilities.SetSupportsChannelScan(false);
    capabilities.SetHandlesDemuxing(false);
    capabilities.SetSupportsRecordingPlayCount(false);
    capabilities.SetSupportsLastPlayedPosition(false);
    capabilities.SetSupportsRecordingEdl(false);
    
    return PVRClientBase::GetAddonCapabilities(capabilities);
}

PVR_ERROR PuzzlePVRClient::CallChannelMenuHook(const kodi::addon::PVRMenuhook& menuhook, const kodi::addon::PVRChannel& item)
{
    if(m_puzzleTV == nullptr)
        return PVR_ERROR_SERVER_ERROR;
    
    if(UPDATE_CHANNEL_STREAMS_MENU_HOOK == menuhook.GetHookId()) {
        HandleStreamsMenuHook(ChannelIdForBrodcastId(item.GetUniqueId()));
        return PVR_ERROR_NO_ERROR;
    } else if (UPDATE_CHANNELS_MENU_HOOK == menuhook.GetHookId()) {
        CreateCoreSafe(false);
        PVR->TriggerChannelUpdate();
        m_clientCore->CallRpcAsync("{\"jsonrpc\": \"2.0\", \"method\": \"GUI.ActivateWindow\", \"params\": {\"window\": \"pvrsettings\"},\"id\": 1}",
                                   [&] (rapidjson::Document& jsonRoot) {
                                        kodi::QueueNotification(QUEUE_INFO, "", kodi::GetLocalizedString(32016));
                                   },
                                   [&](const ActionQueue::ActionResult& s) {});
        return PVR_ERROR_NO_ERROR;
    }
    return PVRClientBase::CallChannelMenuHook(menuhook, item);
}

struct StreamMenuItem{
    StreamMenuItem(const string& title, bool isEnabled = false)
    : Title(title), IsEnabled(isEnabled)
    {}
    
    std::string Title;
    bool IsEnabled;
};

static int ShowStreamsMenu(const string& title, std::vector<StreamMenuItem>& items)
{
    std::vector<string> menu;
    std::vector<int> lut;
    for (size_t i = 0; i < items.size(); ++i)
    {
        if(items[i].IsEnabled) {
            menu.push_back(items[i].Title);
            lut.push_back(i);
        }
    }
    
    int selected = kodi::gui::dialogs::Select::Show(title, menu, -1);
    if(selected < 0)
        return selected;
    return lut[selected];
}

static void FillStreamTitle(const PuzzleTV::PuzzleSource& stream, std::string& title)
{
    title = stream.Server;
}

void PuzzlePVRClient::HandleStreamsMenuHook(ChannelId channelId)
{
    int selected;
    std::string enableStreamLabel(kodi::GetLocalizedString(32054));
    std::string disableStreamLabel(kodi::GetLocalizedString(32055));
    std::string emptyStreamLabel(kodi::GetLocalizedString(32060));
    std::string updateStreamsLabel(kodi::GetLocalizedString(32056));
    
    do {
        PuzzleTV::TPrioritizedSources sources = m_puzzleTV->GetSourcesForChannel(channelId);
        
        StreamMenuItem disableItem(disableStreamLabel, false);
        std::vector<StreamMenuItem> disableMenu;
        StreamMenuItem enableItem(enableStreamLabel, false);
        std::vector<StreamMenuItem> enableMenu;
        StreamMenuItem emptyItem(emptyStreamLabel, false);
        std::vector<StreamMenuItem> emptyMenu;
        
        std::vector<PuzzleTV::TCacheUrl> cacheUrls;
        while(!sources.empty()) {
            const auto source = sources.top();
            sources.pop();
            cacheUrls.push_back(source->first);
            
            disableItem.IsEnabled |= source->second.IsOn() && !source->second.IsEmpty();
            enableItem.IsEnabled |= source->second.CanBeOn();
            emptyItem.IsEnabled |= source->second.IsOn() && source->second.IsEmpty();
            
            // Add to disable menu
            StreamMenuItem disableMenuItem("", source->second.IsOn() && !source->second.IsEmpty());
            FillStreamTitle(source->second, disableMenuItem.Title);
            disableMenu.push_back(disableMenuItem);
            
            // Add to enable menu
            StreamMenuItem enableMenuItem("", source->second.CanBeOn());
            FillStreamTitle(source->second, enableMenuItem.Title);
            enableMenu.push_back(enableMenuItem);
            
            // Add to empty menu
            StreamMenuItem emptyMenuItem("", source->second.IsOn() && source->second.IsEmpty());
            FillStreamTitle(source->second, emptyMenuItem.Title);
            emptyMenu.push_back(emptyMenuItem);
        }
        
        std::vector<StreamMenuItem> rootItems;
        rootItems.push_back(disableItem);
        rootItems.push_back(enableItem);
        rootItems.push_back(emptyItem);
        rootItems.push_back(StreamMenuItem(updateStreamsLabel, true));

        selected = ShowStreamsMenu(kodi::GetLocalizedString(32057), rootItems);
        switch (selected)
        {
            case 0: // Disable stream
            {
                int selectedSource = ShowStreamsMenu(kodi::GetLocalizedString(32058), disableMenu);
                if(selectedSource >= 0) {
                    m_puzzleTV->DisableSource(channelId, cacheUrls[selectedSource]);
                }
                break;
            }
            case 1: // Enable stream
            {
                int selectedSource = ShowStreamsMenu(kodi::GetLocalizedString(32059), enableMenu);
                if(selectedSource >= 0){
                    m_puzzleTV->EnableSource(channelId, cacheUrls[selectedSource]);
                }
                break;
            }
            case 2: // Disable empty stream
            {
                int selectedSource = ShowStreamsMenu(kodi::GetLocalizedString(32058), emptyMenu);
                if(selectedSource >= 0){
                    m_puzzleTV->DisableSource(channelId, cacheUrls[selectedSource]);
                }
                break;
            }
            case 3: // Update stream list
                m_puzzleTV->UpdateChannelSources(channelId);
                break;
            default:
                break;
        }
    } while(selected >= 0);
}

ADDON_STATUS PuzzlePVRClient::OnReloadEpg()
{
    return CreateCoreSafe(true);
}

string PuzzlePVRClient::GetStreamUrl(ChannelId channelId)
{
    m_currentChannelStreamIdx = 0;
    return PVRClientBase::GetStreamUrl(channelId);
}

string PuzzlePVRClient::GetNextStreamUrl(ChannelId channelId)
{
    if(m_puzzleTV == nullptr)
        return string();
    
    kodi::Log(ADDON_LOG_ERROR, "PuzzlePVRClient: trying to move to next stream from [%d].", m_currentChannelStreamIdx);
    return m_puzzleTV->GetNextStream(channelId, m_currentChannelStreamIdx++);
}

void PuzzlePVRClient::OnOpenStremFailed(ChannelId channelId, const std::string& streamUrl)
{
    if(m_puzzleTV == nullptr || !m_blockDeadStreams)
        return;
    
    m_puzzleTV->OnOpenStremFailed(channelId, streamUrl);
    m_currentChannelStreamIdx = 0;
}

bool PuzzlePVRClient::OpenRecordedStream(const kodi::addon::PVRRecording& recording)
{
    if(m_puzzleTV == nullptr)
        return false;

    if(IsLocalRecording(recording))
        return PVRClientBase::OpenRecordedStream(recording);
    
    EpgEntry epgTag;
    if(!m_puzzleTV->GetEpgEntry(std::stoi(recording.GetRecordingId()), epgTag))
        return false;
    
    string url = m_puzzleTV->GetArchiveUrl(epgTag.UniqueChannelId, recording.GetRecordingTime());

    return PVRClientBase::OpenRecordedStream(url, nullptr, IsSeekSupported() ? SupportVodSeek : NoRecordingFlags);
}

PVR_ERROR PuzzlePVRClient::SignalStatus(int channelUid, kodi::addon::PVRSignalStatus& signalStatus)
{
    signalStatus.SetAdapterName("IPTV Puzzle Server");
    signalStatus.SetAdapterStatus((m_puzzleTV == nullptr) ? "Not connected" : "OK");
    
    string liveUrl = GetLiveUrl();
    string serviceName;
    
    if(!liveUrl.empty()) {
        PuzzleTV::TPrioritizedSources sources = m_puzzleTV->GetSourcesForChannel(GetLiveChannelId());
        while(!sources.empty()) {
            const auto source = sources.top();
            sources.pop();
            for (const auto& stream : source->second.Streams) {
                if(stream.first == liveUrl) {
                    serviceName = source->second.Server;
                    break;
                }
            }
            if(!serviceName.empty()) {
                break;
            }
        }
        signalStatus.SetProviderName(serviceName);
    }
    
    return PVRClientBase::SignalStatus(channelUid, signalStatus);
}
