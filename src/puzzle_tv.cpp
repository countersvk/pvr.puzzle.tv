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

#include <rapidjson/document.h>
#include <rapidjson/error/en.h>
#include <cassert>
#include <algorithm>
#include <string>
#include <cstdlib>
#include <ctime>
#include <list>
#include <chrono>
#include <thread>
#include "kodi/General.h"

#include "helpers.h"
#include "puzzle_tv.h"
#include "HttpEngine.hpp"
#include "XMLTV_loader.hpp"
#include "globals.hpp"
#include "base64.h"

using namespace Globals;
using namespace std;
using namespace rapidjson;
using namespace PuzzleEngine;
using namespace PvrClient;
using namespace Helpers;

static const int secondsPerHour = 60 * 60;
static const char* c_EpgCacheFile = "puzzle_epg_cache.txt";

static void DumpStreams(const PuzzleTV::TPrioritizedSources& s)
{
    PuzzleTV::TPrioritizedSources sources(s);
    while (!sources.empty()) {
        const auto& source = sources.top()->second;
        const auto& streams = source.Streams;
        for_each(streams.begin(), streams.end(), [&source](PuzzleTV::PuzzleSource::TStreamsQuality::const_reference stream) {
            LogDebug("URL %s: %s", source.Server.c_str(), stream.first.c_str());
        });
        sources.pop();
    }
}

struct PuzzleTV::ApiFunctionData
{
    ApiFunctionData(const char* _name, uint16_t _port, const ParamList* _params = nullptr)
    : name(_name), port(_port), attempt(0)
    {
        if(_params){
            params = *_params;
        }
    }
    
    const std::string name;
    const uint16_t port;
    ParamList params;
    mutable int attempt;
};

static bool IsAceUrl(const std::string& url, std::string& aceServerUrlBase)
{
    auto pos = url.find(":6878/ace/");
    const bool isAce = pos != string::npos;
    if(isAce) {
        const char* httpStr = "http://";
        auto startPos = url.find(httpStr);
        if(string::npos != startPos){
            startPos += strlen(httpStr);
            aceServerUrlBase = url.substr(startPos, pos - startPos);
        }
    }
    return isAce;
}

static std::string ToPuzzleChannelId(PvrClient::ChannelId channelId){
    string strId = n_to_string_hex(channelId);
    int leadingZeros = 8 - strId.size();
    while(leadingZeros--)
        strId = "0" + strId;
    return strId;
}

std::string PuzzleTV::EpgUrlForPuzzle3() const {
    return string("http://") + m_serverUri + ":" + n_to_string(m_serverPort) + "/epg/xmltv";
}

PuzzleTV::PuzzleTV(ServerVersion serverVersion, const char* serverUrl, uint16_t serverPort) :
    m_serverUri(serverUrl),
    m_serverPort(serverPort),
    m_epgServerPort(8085),
    m_epgUrl("https://iptvx.one/epg/epg.xml.gz"),
    m_serverVersion(serverVersion),
    m_isAceRunning(false),
    m_maxServerRetries(4)
{
}

void PuzzleTV::Init(bool clearEpgCache)
{
    if(clearEpgCache)
        ClearEpgCache(c_EpgCacheFile, m_epgUrl.c_str());
    
    RebuildChannelAndGroupList();
    
    if(!clearEpgCache)
        LoadEpgCache(c_EpgCacheFile);

    UpdateArhivesAsync();
}

PuzzleTV::~PuzzleTV()
{
    Cleanup();
    PrepareForDestruction();
}

void PuzzleTV::Cleanup()
{
    // Cleanup implementation
}

void PuzzleTV::BuildChannelAndGroupList()
{
    struct NoCaseComparator
    {
        inline bool operator()(const string& x, const string& y) const
        {
            return StringUtils::CompareNoCase(x, y) < 0;
        }
    };

    struct GroupWithIndex{
        GroupWithIndex(string g, int idx = 0) : name(g), index(idx) {}
        string name;
        int index;
    };
    
    typedef vector<GroupWithIndex> GroupsWithIndex;
    typedef map<string, pair<Channel, GroupsWithIndex>, NoCaseComparator> PlaylistContent;

    try {
        PlaylistContent plistContent;
        const bool isPuzzle2 = m_serverVersion == c_PuzzleServer2;
        
        const char* cmd = isPuzzle2 ? "/get/json" : "/channels/json";
        ApiFunctionData params(cmd, m_serverPort);
        
        CallApiFunction(params, [&plistContent, isPuzzle2](Document& jsonRoot)
        {
            const Value &channels = jsonRoot["channels"];
            unsigned int channelNumber = 0;
            for(auto itChannel = channels.Begin(); itChannel != channels.End(); ++itChannel)
            {
                Channel channel;
                char* dummy;
                channel.UniqueId = channel.EpgId = strtoul((*itChannel)["id"].GetString(), &dummy, 16);
                
                if(itChannel->HasMember("num")){
                    channel.Number = (*itChannel)["num"].GetInt();
                } else {
                    channel.Number = ++channelNumber;
                }
                
                channel.Name = (*itChannel)["name"].GetString();
                channel.IconPath = (*itChannel)["icon"].GetString();
                channel.IsRadio = false;
                channel.HasArchive = false;
                
                if(isPuzzle2)
                    channel.Urls.push_back((*itChannel)["url"].GetString());
                
                GroupsWithIndex groups;
                if(itChannel->HasMember("group_num")) {
                    const auto& jGroups = (*itChannel)["group_num"];
                    for(auto groupIt = jGroups.Begin(); groupIt < jGroups.End(); ++groupIt){
                        groups.emplace_back((*groupIt)["name"].GetString(), (*groupIt)["num"].GetInt());
                    }
                } else {
                    const auto& jGroups = (*itChannel)["group"];
                    if(jGroups.IsString()) {
                        groups.emplace_back(jGroups.GetString());
                    } else if(jGroups.IsArray()) {
                        for(auto groupIt = jGroups.Begin(); groupIt < jGroups.End(); ++groupIt){
                            groups.emplace_back(groupIt->GetString());
                        }
                    }
                }
                plistContent[channel.Name] = make_pair(channel, groups);
            }
        });
        
        if(m_epgType == c_EpgType_File) {
            m_epgToServerLut.clear();
            using namespace XMLTV;
            
            auto pThis = this;
            ChannelCallback onNewChannel = [&plistContent, pThis](const EpgChannel& newChannel){
                for(const auto& epgChannelName : newChannel.displayNames) {
                    if(plistContent.count(epgChannelName) != 0) {
                        auto& plistChannel = plistContent[epgChannelName].first;
                        pThis->m_epgToServerLut[newChannel.id] = plistChannel.EpgId;
                        if(plistChannel.IconPath.empty())
                            plistChannel.IconPath = newChannel.strIcon;
                    }
                }
            };
            
            XMLTV::ParseChannels(m_epgUrl, onNewChannel);
        }

        for(const auto& channelWithGroup : plistContent)
        {
            const auto& channel = channelWithGroup.second.first;
            AddChannel(channel);
            
            for (const auto& groupWithIndex : channelWithGroup.second.second) {
                const auto& groupList = m_groupList;
                auto itGroup = find_if(groupList.begin(), groupList.end(), [&](const GroupList::value_type& v){
                    return groupWithIndex.name == v.second.Name;
                });
                
                if (itGroup == groupList.end()) {
                    Group newGroup;
                    newGroup.Name = groupWithIndex.name;
                    AddGroup(groupList.size(), newGroup);
                    itGroup = --groupList.end();
                }
                AddChannelToGroup(itGroup->first, channel.UniqueId, groupWithIndex.index);
            }
        }
       
    } catch (ServerErrorException& ex) {
        kodi::Log(ADDON_LOG_ERROR, "PuzzleTV: Server error: %s", ex.reason.c_str());
    } catch (exception& ex) {
        kodi::Log(ADDON_LOG_ERROR, "PuzzleTV: FAILED to build channel list. Exception: %s", ex.what());
    } catch (...) {
        kodi::Log(ADDON_LOG_ERROR, "PuzzleTV: FAILED to build channel list");
    }
}

PvrClient::UniqueBroadcastIdType PuzzleTV::AddXmlEpgEntry(const XMLTV::EpgEntry& xmlEpgEntry)
{
    if(m_epgToServerLut.count(xmlEpgEntry.EpgId) == 0) {
        return c_UniqueBroadcastIdUnknown;
    }
    
    unsigned int id = (unsigned int)xmlEpgEntry.startTime;

    EpgEntry epgEntry;
    epgEntry.UniqueChannelId = m_epgToServerLut[xmlEpgEntry.EpgId];
    epgEntry.Title = xmlEpgEntry.strTitle;
    epgEntry.Description = xmlEpgEntry.strPlot;
    epgEntry.StartTime = xmlEpgEntry.startTime;
    epgEntry.EndTime = xmlEpgEntry.endTime;
    epgEntry.IconPath = xmlEpgEntry.iconPath;
    return AddEpgEntry(id, epgEntry);
}

void PuzzleTV::UpdateEpgForAllChannels(time_t startTime, time_t endTime, function<bool(void)> cancelled)
{
    try {
        LoadEpg(cancelled);
        if(!cancelled())
            SaveEpgCache(c_EpgCacheFile);
    } catch (exception& ex) {
        kodi::Log(ADDON_LOG_ERROR, "PuzzleTV: FAILED receive EPG. Exception: %s", ex.what());
    } catch (...) {
        kodi::Log(ADDON_LOG_ERROR, "PuzzleTV: FAILED receive EPG");
    }
}

static bool time_compare(const EpgEntry& first, const EpgEntry& second)
{
    return first.StartTime < second.StartTime;
}

void PuzzleTV::LoadEpg(function<bool(void)> cancelled)
{
    auto pThis = this;
    m_epgUpdateInterval.Init(12*60*60*1000);

    if(m_epgType == c_EpgType_File) {
        XMLTV::EpgEntryCallback onEpgEntry = [pThis, cancelled](const XMLTV::EpgEntry& newEntry) {
            pThis->AddXmlEpgEntry(newEntry);
            return !cancelled();
        };
        XMLTV::ParseEpg(m_epgUrl, onEpgEntry);
    } else if(m_serverVersion == c_PuzzleServer2) {
        auto pThis = this;
        long offset = -(3 * 60 * 60) - XMLTV::LocalTimeOffset();
        
        ApiFunctionData apiParams("/channel/json/id=all", m_epgServerPort);
        try {
            CallApiFunction(apiParams, [pThis, offset](Document& jsonRoot) {
                if(!jsonRoot.IsObject()) {
                    kodi::Log(ADDON_LOG_ERROR, "PuzzleTV: wrong JSON format of EPG");
                    return;
                }
                
                for(auto i = jsonRoot.MemberBegin(); i != jsonRoot.MemberEnd(); ++i) {
                    if(i->value.IsObject() && i->value.HasMember("title") && !i->value.HasMember("plot")) {
                        auto channelIdStr = i->name.GetString();
                        char* dummy;
                        ChannelId channelId = strtoul(channelIdStr, &dummy, 16);
                        
                        list<EpgEntry> serverEpg;
                        auto& epgObj = i->value;
                        for(auto epgItem = epgObj.MemberBegin(); epgItem != epgObj.MemberEnd(); ++epgItem) {
                            if(epgItem->value.IsObject() && epgItem->value.HasMember("plot") && 
                               epgItem->value.HasMember("img") && epgItem->value.HasMember("title")) {
                                EpgEntry epgEntry;
                                epgEntry.UniqueChannelId = channelId;
                                string s = epgItem->name.GetString();
                                s = s.substr(0, s.find('.'));
                                unsigned long l = stoul(s.c_str());
                                epgEntry.StartTime = (time_t)l + offset;
                                epgEntry.Title = epgItem->value["title"].GetString();
                                epgEntry.Description = epgItem->value["plot"].GetString();
                                serverEpg.push_back(epgEntry);
                            }
                        }
                        
                        serverEpg.sort(time_compare);
                        auto runner = serverEpg.begin();
                        auto end = serverEpg.end();
                        if(runner != end){
                            auto pItem = runner++;
                            while(runner != end) {
                                pItem->EndTime = runner->StartTime;
                                pThis->AddEpgEntry(pItem->StartTime, *pItem);
                                ++runner; 
                                ++pItem;
                            }
                        }
                    }
                }
            });
        } catch (exception& ex) {
            kodi::Log(ADDON_LOG_ERROR, "PuzzleTV: exception on loading JSON EPG: %s", ex.what());
        } catch (...) {
            kodi::Log(ADDON_LOG_ERROR, "PuzzleTV: exception on loading JSON EPG");
        }
    } else {
        kodi::Log(ADDON_LOG_ERROR, "PuzzleTV: unknown EPG source type %d", m_epgType);
    }
}

bool PuzzleTV::CheckChannelId(ChannelId channelId)
{
    if(m_channelList.count(channelId) != 1) {
        kodi::Log(ADDON_LOG_ERROR, "PuzzleTV::CheckChannelId: Unknown channel ID= %d", channelId);
        return false;
    }
    return true;
}

#pragma mark - Archive
void PuzzleTV::UpdateHasArchive(PvrClient::EpgEntry& entry)
{
    auto pChannel = find_if(m_channelList.begin(), m_channelList.end(), [&entry](const ChannelList::value_type& ch) {
        return ch.second.UniqueId == entry.UniqueChannelId;
    });
    
    entry.HasArchive = pChannel != m_channelList.end() && pChannel->second.HasArchive;
    
    if(!entry.HasArchive)
        return;
    
    time_t now = time(nullptr);
    time_t epgTime = entry.EndTime;
    
    switch(m_addCurrentEpgToArchive) {
        case PvrClient::k_AddCurrentEpgToArchive_Yes:
            epgTime = entry.StartTime;
            break;
        case PvrClient::k_AddCurrentEpgToArchive_AfterInit:
        {
            auto phase = GetPhase(k_RecordingsInitialLoadingPhase);
            epgTime = phase->IsDone() ? entry.StartTime : entry.EndTime;
            break;
        }
        default:
            break;
    }
    
    const time_t archivePeriod = 3 * 24 * 60 * 60;
    time_t from = now - archivePeriod;
    entry.HasArchive = epgTime > from && epgTime < now;
}

void PuzzleTV::UpdateArhivesAsync()
{
    if(m_serverVersion == c_PuzzleServer2) {
        return;
    }
    
    auto pThis = this;
    TArchiveInfo* localArchiveInfo = new TArchiveInfo();
    ApiFunctionData data("/archive/json/list", m_serverPort);
    
    CallApiAsync(data,
        [localArchiveInfo, pThis](Document& jsonRoot) {
            if(!jsonRoot.IsArray()) {
                kodi::Log(ADDON_LOG_ERROR, "PuzzleTV::UpdateArhivesAsync(): bad Puzzle Server response (not array).");
                return;
            }
            
            for (const auto& ch : jsonRoot.GetArray()) {
                if(!ch.IsObject() || !ch.HasMember("name")){
                    continue;
                }
                
                const auto& chName = ch["name"].GetString();
                if(!ch.HasMember("id") || !ch.HasMember("cid")) {
                    kodi::Log(ADDON_LOG_ERROR, "PuzzleTV::UpdateArhivesAsync(): channel %s does not have ID or archive ID.", chName);
                    continue;
                }
                
                const auto& archiveChId = ch["id"].GetString();
                const auto& chId = ch["cid"].GetString();
                char* dummy;
                ChannelId id = strtoul(chId, &dummy, 16);
                
                try {
                    Channel channelWithArcive(pThis->m_channelList.at(id));
                    channelWithArcive.HasArchive = true;
                    pThis->AddChannel(channelWithArcive);
                    
                    ChannelArchiveInfo chInfo;
                    chInfo.archiveId = archiveChId;
                    (*localArchiveInfo)[id] = chInfo;
                } catch (out_of_range&) {
                    kodi::Log(ADDON_LOG_ERROR, "PuzzleTV::UpdateArhivesAsync(): Unknown archive channel %s (%s)", chName, chId);
                } catch (...) {
                    kodi::Log(ADDON_LOG_ERROR, "PuzzleTV::UpdateArhivesAsync(): unknown error for channel %s (%s)", chName, chId);
                }
            }
        },
        [localArchiveInfo, pThis](const ActionQueue::ActionResult& s) {
            if(s.status == ActionQueue::kActionCompleted) {
                lock_guard<mutex> lock(pThis->m_archiveAccessMutex);
                pThis->m_archiveInfo = *localArchiveInfo;
            }
            delete localArchiveInfo;
        });
}

std::string PuzzleTV::GetArchiveUrl(ChannelId channelId, time_t startTime)
{
    string url;
    string recordId = GetRecordId(channelId, startTime);
    
    if(recordId.empty())
        return url;
    
    string command("/archive/json/records/");
    command += recordId;
    
    ApiFunctionData data(command.c_str(), m_serverPort);
    try {
        CallApiFunction(data, [&url, recordId](Document& jsonRoot) {
            if(!jsonRoot.IsArray()) {
                kodi::Log(ADDON_LOG_ERROR, "PuzzleTV::GetArchiveUrl(): wrong JSON format (not array). RID=%s", recordId.c_str());
                return;
            }
            url = jsonRoot.Begin()->GetString();
        });
    } catch(...) {
        // Handle exception
    }
    
    return url;
}

std::string PuzzleTV::GetRecordId(ChannelId channelId, time_t startTime) {
    long offset = -(3 * 60 * 60) - XMLTV::LocalTimeOffset();
    startTime += offset;
    
    if(m_serverVersion == c_PuzzleServer2) {
        return string();
    }
    
    string archiveId;
    {
        lock_guard<mutex> lock(m_archiveAccessMutex);
        if(m_archiveInfo.count(channelId) == 0) {
            return string();
        }
        
        ChannelArchiveInfo& channelArchiveInfo = m_archiveInfo[channelId];
        archiveId = channelArchiveInfo.archiveId;

        if(channelArchiveInfo.records.count(startTime) != 0) {
            return channelArchiveInfo.records[startTime].id;
        }
    }
    
    time_t now = time(nullptr);
    if(startTime > now)
        return string();
    
    struct tm* t = localtime(&now);
    int day_now = t->tm_yday;
    t = localtime(&startTime);
    int day_start = t->tm_yday;
    
    if(day_now < day_start) {
        day_now += 365;
    }
    int day = (day_now - day_start);
    
    auto pThis = this;
    string command("/archive/json/id/");
    command += archiveId + "/day/" + n_to_string(day);

    ApiFunctionData data(command.c_str(), m_serverPort);
    TArchiveRecords* records = new TArchiveRecords();
    
    try {
        CallApiFunction(data, [records, archiveId](Document& jsonRoot) {
            if(!jsonRoot.IsObject()) {
                kodi::Log(ADDON_LOG_ERROR, "PuzzleTV: wrong JSON format of archive info. AID=%s", archiveId.c_str());
                return;
            }
            
            for(auto i = jsonRoot.MemberBegin(); i != jsonRoot.MemberEnd(); ++i) {
                if(i->value.IsObject() && i->value.HasMember("id") && i->value.HasMember("s_time")) {
                    auto& arObj = i->value;
                    double t = arObj["s_time"].GetDouble();
                    auto& record = (*records)[t];
                    record.id = arObj["id"].GetString();
                }
            }
        });
        
        {
            lock_guard<mutex> lock(pThis->m_archiveAccessMutex);
            pThis->m_archiveInfo[channelId].records.insert(records->begin(), records->end());
            delete records;
            
            if(m_archiveInfo[channelId].records.count(startTime) != 0) {
                return m_archiveInfo[channelId].records[startTime].id;
            }
        }
    } catch (...) {
        kodi::Log(ADDON_LOG_ERROR, "PuzzleTV::GetRecordId(): FAILED to obtain recordings for channel %d, day %d", channelId, day);
        delete records;
    }
    
    return string();
}

#pragma mark - Streams

string PuzzleTV::GetUrl(ChannelId channelId)
{
    if(!CheckChannelId(channelId)) {
        return string();
    }
    
    if(m_channelList.at(channelId).Urls.empty()){
        UpdateChannelSources(channelId);
    }
    
    string url;
    auto sources = GetSourcesForChannel(channelId);
    
    while (!sources.empty()) {
        const auto& streams = sources.top()->second.Streams;
        auto goodStream = find_if(streams.begin(), streams.end(), [](PuzzleSource::TStreamsQuality::const_reference stream) {
            return stream.second;
        });
        
        if(goodStream != streams.end()) {
            url = goodStream->first;
            break;
        }
        sources.pop();
    }
    
    if(url.empty()) {
        kodi::Log(ADDON_LOG_ERROR, "PuzzleTV: No available streams for channel");
        return url;
    }
    
    string aceServerUrlBase;
    if(IsAceUrl(url, aceServerUrlBase)){
        if(!CheckAceEngineRunning(aceServerUrlBase.c_str()))  {
            kodi::Log(ADDON_LOG_ERROR, "PuzzleTV: Ace Engine not running");
            return string();
        }
    }
    
    return url;
}

string PuzzleTV::GetNextStream(ChannelId channelId, int currentStreamIdx)
{
    if(!CheckChannelId(channelId))
        return string();

    string url;
    auto sources = GetSourcesForChannel(channelId);
    bool isFound = false;
    int goodStreamsIdx = -1;
    
    while (!isFound && !sources.empty()) {
        const auto& streams = sources.top()->second.Streams;
        auto goodStream = find_if(streams.begin(), streams.end(), [](PuzzleSource::TStreamsQuality::const_reference stream) {
            return stream.second;
        });
        
        if(goodStream != streams.end()) {
            url = goodStream->first;
            ++goodStreamsIdx;
            
            string aceServerUrlBase;
            if(!IsAceUrl(url, aceServerUrlBase)){
                isFound = goodStreamsIdx > currentStreamIdx;
            } else if(CheckAceEngineRunning(aceServerUrlBase.c_str())) {
                isFound = goodStreamsIdx > currentStreamIdx;
            }
        }
        sources.pop();
    }
    
    return isFound ? url : string();
}

void PuzzleTV::OnOpenStremFailed(ChannelId channelId, const std::string& streamUrl)
{
    TChannelSources& sources = m_sources[channelId];
    for(auto& source : sources) {
        if(source.second.Streams.count(streamUrl) != 0){
            source.second.Streams.at(streamUrl) = false;
            if (none_of(source.second.Streams.begin(), source.second.Streams.end(), 
                       [](const PuzzleSource::TStreamsQuality::value_type& isGood){ return isGood.second; })) {
                DisableSource(channelId, source.first);
            }
            break;
        }
    }
}

void PuzzleTV::UpdateUrlsForChannel(PvrClient::ChannelId channelId)
{
    if(!CheckChannelId(channelId))
        return;
 
    Channel ch = m_channelList.at(channelId);
    auto& urls = ch.Urls;
    urls.clear();
    
    try {
        auto pThis = this;
        const string strId = ToPuzzleChannelId(channelId);
        
        if(m_serverVersion == c_PuzzleServer2){
            string cmd = string("/get/streams/") + strId;
            ApiFunctionData apiParams(cmd.c_str(), m_serverPort);
            CallApiFunction(apiParams, [&urls, pThis](Document& jsonRoot)
            {
                if(!jsonRoot.IsArray())
                    return;
                    
                for(auto i = jsonRoot.Begin(); i != jsonRoot.End(); ++i)
                {
                    auto url = pThis->TranslateMultucastUrl(i->GetString());
                    urls.push_back(url);
                }
            });
        } else {
            auto& cacheSources = m_sources[channelId];
            string cmd = string("/streams/json_ds/") + strId;
            ApiFunctionData apiParams(cmd.c_str(), m_serverPort);
            
            CallApiFunction(apiParams, [&urls, &cacheSources, pThis](Document& jsonRoot)
            {
                if(!jsonRoot.IsArray())
                    return;

                for(auto s = jsonRoot.Begin(); s != jsonRoot.End(); ++s)
                {
                    if(!s->HasMember("cache") || !s->HasMember("streams")) {
                        throw MissingApiException("Missing required fields in JSON response");
                    }
                    
                    auto cacheUrl = (*s)["cache"].GetString();
                    PuzzleSource& source = cacheSources[cacheUrl];

                    auto streams = (*s)["streams"].GetArray();
                    for(auto st = streams.Begin(); st != streams.End(); ++st){
                        auto url = pThis->TranslateMultucastUrl(st->GetString());
                        urls.push_back(url);
                        source.Streams[url] = true;
                    }
                }
            });
        }
        
        AddChannel(ch);
    } catch (ServerErrorException& ex) {
        kodi::Log(ADDON_LOG_ERROR, "PuzzleTV: Server error: %s", ex.reason.c_str());
    } catch (MissingApiException& ex){
        kodi::Log(ADDON_LOG_ERROR, "PuzzleTV: Bad JSON response: %s", ex.what());
    } catch (exception& ex) {
        kodi::Log(ADDON_LOG_ERROR, "PuzzleTV: FAILED to get URL for channel ID=%d. Exception: %s", channelId, ex.what());
    } catch (...) {
        kodi::Log(ADDON_LOG_ERROR, "PuzzleTV: FAILED to get URL for channel ID=%d", channelId);
    }
}

void PuzzleTV::UpdateChannelSources(ChannelId channelId)
{
    if(m_serverVersion == c_PuzzleServer2 || !CheckChannelId(channelId))
        return;
    
    TChannelSources sources;
    try {
        string cmd = string("/cache_url/") + ToPuzzleChannelId(channelId) + "/json";
        ApiFunctionData apiParams(cmd.c_str(), m_serverPort);
        
        CallApiFunction(apiParams, [&sources](Document& jsonRoot)
        {
            if(!jsonRoot.IsArray())
                return;
                
            for(auto i = jsonRoot.Begin(); i != jsonRoot.End(); ++i)
            {
                if(!i->HasMember("url")) {
                    continue;
                }
                
                TCacheUrl cacheUrl = (*i)["url"].GetString();
                PuzzleSource& source = sources[cacheUrl];

                if(i->HasMember("serv")) {
                    source.Server = (*i)["serv"].GetString();
                } else {
                    source.Server = cacheUrl.find("acesearch") != string::npos ? "ASE" : "HTTP";
                }
                
                if(i->HasMember("lock")) {
                    source.IsChannelLocked = (*i)["lock"].GetBool();
                }
                
                if(i->HasMember("serv_on")) {
                    source.IsServerOn = (*i)["serv_on"].GetBool();
                }
                
                if(i->HasMember("priority")) {
                    auto s = (*i)["priority"].GetString();
                    if(strlen(s) > 0) {
                        source.Priority = atoi(s);
                    }
                }
                
                if(i->HasMember("id")) {
                    auto s = (*i)["id"].GetString();
                    if(strlen(s) > 0) {
                        source.Id = atoi(s);
                    }
                }
            }
        });
    } catch (ServerErrorException& ex) {
        kodi::Log(ADDON_LOG_ERROR, "PuzzleTV: Server error: %s", ex.reason.c_str());
    } catch (exception& ex) {
        kodi::Log(ADDON_LOG_ERROR, "PuzzleTV: FAILED to get sources list for channel ID=%d. Exception: %s", channelId, ex.what());
    } catch (...) {
        kodi::Log(ADDON_LOG_ERROR, "PuzzleTV: FAILED to get sources list for channel ID=%d", channelId);
    }

    m_sources[channelId] = sources;
    UpdateUrlsForChannel(channelId);
}

PuzzleTV::TPrioritizedSources PuzzleTV::GetSourcesForChannel(ChannelId channelId)
{
    if(m_sources.count(channelId) == 0) {
        UpdateChannelSources(channelId);
    }
    
    TPrioritizedSources result;
    const auto& sources = m_sources[channelId];

    for (const auto& source : sources) {
        result.push(&source);
    }
    return result;
}

void PuzzleTV::EnableSource(PvrClient::ChannelId channelId, const TCacheUrl& cacheUrl)
{
    if(m_sources.count(channelId) == 0)
        return;
        
    for (auto& source : m_sources[channelId]) {
        if(source.first == cacheUrl && !source.second.IsOn()){
            source.second.IsChannelLocked = false;
            
            const string strId = ToPuzzleChannelId(channelId);
            string encoded = base64_encode(reinterpret_cast<const unsigned char*>(cacheUrl.c_str()), cacheUrl.length());
            string cmd = string("/black_list/") + encoded + "/unlock/" + strId + "/nofollow";
            
            ApiFunctionData apiParams(cmd.c_str(), m_serverPort);
            CallApiAsync(apiParams, [](Document&){}, [channelId, strId](const ActionQueue::ActionResult& s) {
                if(s.exception){
                    kodi::Log(ADDON_LOG_ERROR, "PuzzleTV: FAILED to enable source for channel %s", strId.c_str());
                }
            });
        
            UpdateChannelSources(channelId);
            break;
        }
    }
}

void PuzzleTV::DisableSource(PvrClient::ChannelId channelId, const TCacheUrl& cacheUrl)
{
    if(m_sources.count(channelId) == 0)
        return;
        
    for (auto& source : m_sources[channelId]) {
        if(source.first == cacheUrl && source.second.IsOn()){
            source.second.IsChannelLocked = true;
            
            const string strId = ToPuzzleChannelId(channelId);
            string encoded = base64_encode(reinterpret_cast<const unsigned char*>(cacheUrl.c_str()), cacheUrl.length());
            string cmd = string("/black_list/") + encoded + "/lock/" + strId + "/nofollow";
            
            ApiFunctionData apiParams(cmd.c_str(), m_serverPort);
            CallApiAsync(apiParams, [](Document&){}, [channelId, strId](const ActionQueue::ActionResult& s) {
                if(s.exception){
                    kodi::Log(ADDON_LOG_ERROR, "PuzzleTV: FAILED to disable source for channel %s", strId.c_str());
                }
            });
            
            UpdateChannelSources(channelId);
            break;
        }
    }
}

#pragma mark - API Call

template <typename TParser>
void PuzzleTV::CallApiFunction(const ApiFunctionData& data, TParser parser)
{
    std::mutex eventMutex;
    std::condition_variable event;
    bool completed = false;
    std::exception_ptr ex = nullptr;
    
    CallApiAsync(data, parser, [&ex, &completed, &event](const ActionQueue::ActionResult& s) {
        ex = s.exception;
        completed = true;
        event.notify_all();
    });
    
    std::unique_lock<std::mutex> lock(eventMutex);
    event.wait(lock, [&completed] { return completed; });
    
    if(ex) {
        try {
            std::rethrow_exception(ex);
        } catch (JsonParserException& jex) {
            kodi::Log(ADDON_LOG_ERROR, "Puzzle server JSON error: %s", jex.what());
            throw;
        } catch (CurlErrorException& cex) {
            if(data.attempt >= m_maxServerRetries - 1)
                throw cex;
                
            data.attempt += 1;
            this_thread::sleep_for(chrono::seconds(4));
            CallApiFunction(data, parser);
        }
    }
}

template <typename TParser, typename TCompletion>
void PuzzleTV::CallApiAsync(const ApiFunctionData& data, TParser parser, TCompletion completion)
{
    string query;
    auto runner = data.params.begin();
    auto first = runner;
    auto end = data.params.end();
    
    for (; runner != end; ++runner)
    {
        query += (runner == first) ? "?" : "&";
        query += runner->first + '=' + runner->second;
    }
    
    string strRequest = string("http://") + m_serverUri + ":";
    strRequest += n_to_string(data.port);
    strRequest += data.name + query;

    CallApiAsync(strRequest, data.name, parser, completion);
}

template <typename TParser, typename TCompletion>
void PuzzleTV::CallApiAsync(const std::string& strRequest, const std::string& name, TParser parser, TCompletion completion)
{
    auto start = chrono::steady_clock::now();

    auto pThis = this;
    
    function<void(const string&)> parserWrapper = [pThis, start, parser, name](const string& response) {
        auto end = chrono::steady_clock::now();
        auto duration = chrono::duration_cast<chrono::milliseconds>(end - start);
        
        pThis->ParseJson(response, [&parser](Document& jsonRoot) {
            parser(jsonRoot);
        });
    };

    m_httpEngine->CallApiAsync(strRequest, parserWrapper, completion);
}

bool PuzzleTV::CheckAceEngineRunning(const char* aceServerUrlBase)
{
    static time_t last_check = 0;
    if (m_isAceRunning || difftime(time(nullptr), last_check) < 60) {
        return m_isAceRunning;
    }
    
    bool isRunning = false;
    std::mutex eventMutex;
    std::condition_variable event;
    bool completed = false;
    
    try {
        string strRequest = string("http://") + aceServerUrlBase + ":6878/webui/api/service?method=get_version&format=jsonp&callback=mycallback";
        m_httpEngine->CallApiAsync(strRequest, 
            [&isRunning](const string& response) {
                isRunning = response.find("version") != string::npos;
            }, 
            [&isRunning, &completed, &event](const ActionQueue::ActionResult& s) {
                if(s.status != ActionQueue::kActionCompleted)
                    isRunning = false;
                completed = true;
                event.notify_all();
            }, 
            HttpEngine::RequestPriority_Hi);
            
        std::unique_lock<std::mutex> lock(eventMutex);
        event.wait(lock, [&completed] { return completed; });
        
    } catch (exception& ex) {
        kodi::Log(ADDON_LOG_ERROR, "Puzzle TV: CheckAceEngineRunning() STD exception: %s", ex.what());
    } catch (...) {
        kodi::Log(ADDON_LOG_ERROR, "Puzzle TV: CheckAceEngineRunning() unknown exception.");
    }
    
    last_check = time(nullptr);
    return m_isAceRunning = isRunning;
}
