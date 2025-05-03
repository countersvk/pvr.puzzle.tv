#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <functional>
#include <chrono>
#include <mutex>
#include <shared_mutex>
#include <span>
#include <optional>
#include <kodi/AddonBase.h>
#include <rapidjson/document.h>
#include "pvr_client_types.h"
#include "ActionQueueTypes.hpp"

namespace PvrClient {

namespace chrono = std::chrono;
using namespace std::chrono_literals;

class ClientCoreBase : public IClientCore {
public:
    explicit ClientCoreBase(RecordingsDelegate recordings_delegate = nullptr);
    virtual ~ClientCoreBase() = default;

    void InitAsync(bool clear_epg_cache, bool update_recordings) override;
    
    std::shared_ptr<IPhase> GetPhase(Phase phase) override;
    
    // Channel and group management
    const ChannelList& GetChannelList() override;
    const GroupList& GetGroupList() override;
    GroupId GroupForChannel(ChannelId ch_id) override;
    void RebuildChannelAndGroupList() override;

    // EPG management
    bool GetEpgEntry(UniqueBroadcastIdType id, EpgEntry& entry) override;
    void GetEpg(ChannelId channel_id, chrono::system_clock::time_point start, 
               chrono::system_clock::time_point end, EpgEntryAction& on_epg_entry) override;

    // RPC configuration
    void SetRpcSettings(RpcSettings settings) { m_rpc_settings = std::move(settings); }
    void CheckRpcConnection();
    
    // Other configurations
    void SetEpgCorrectionShift(chrono::seconds shift) { m_epg_correction = shift; }
    void SupportMulticastUrls(bool support, std::string_view proxy_host = {}, uint16_t proxy_port = 0);

protected:
    struct EpgCache {
        std::filesystem::path path;
        chrono::system_clock::time_point expiration;
    };

    // Abstract interface
    virtual void UpdateEpgForAllChannels(chrono::system_clock::time_point start, 
                                        chrono::system_clock::time_point end,
                                        std::stop_token stop_token) = 0;
    virtual std::string GetUrl(ChannelId channel_id) = 0;

    // Helper methods
    void LoadEpgCache(std::string_view cache_file);
    void SaveEpgCache(std::string_view cache_file, chrono::hours ttl = 7*24h);
    
    void AddChannel(Channel channel);
    void AddGroup(GroupId group_id, Group group);
    void UpdateChannelLogo(Channel& channel) const;

private:
    // Implementation details
    struct PhaseData {
        std::shared_ptr<ClientPhase> phase;
        std::shared_mutex mutex;
    };

    void InitializeComponents(bool clear_epg_cache);
    void ScheduleEpgUpdate();
    void ProcessEpgEntries(std::span<const EpgEntry> entries);

    // Data members
    std::unique_ptr<HttpEngine> m_http_engine;
    std::unordered_map<Phase, PhaseData> m_phases;
    
    ChannelList m_channels;
    GroupList m_groups;
    std::unordered_map<ChannelId, GroupId> m_channel_group_map;
    
    std::unordered_map<UniqueBroadcastIdType, EpgEntry> m_epg_entries;
    mutable std::shared_mutex m_epg_mutex;
    
    RecordingsDelegate m_recordings_delegate;
    RpcSettings m_rpc_settings;
    chrono::seconds m_epg_correction{0};
    
    std::string m_local_logos_path;
    bool m_support_multicast = false;
    std::string m_multicast_proxy;
};

// Modern exception hierarchy
class PvrException : public std::exception {
public:
    explicit PvrException(std::string_view message)
        : m_message(message) {}
    
    const char* what() const noexcept override { return m_message.c_str(); }

private:
    std::string m_message;
};

class JsonError : public PvrException {
public:
    using PvrException::PvrException;
};

class RpcError : public PvrException {
public:
    using PvrException::PvrException;
};

} // namespace PvrClient
