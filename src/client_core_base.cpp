#include <chrono>
#include <filesystem>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>
#include <optional>
#include <span>
#include <kodi/AddonBase.h>
#include <kodi/Filesystem.h>
#include <rapidjson/document.h>
#include <rapidjson/error/en.h>

namespace fs = std::filesystem;
using namespace std::chrono;

namespace PvrClient {

namespace {
    constexpr auto EPG_CACHE_DIR = "special://temp/pvr-puzzle-tv";
    constexpr auto EPG_CACHE_TTL = 7 * 24h;
}

class ClientPhase : public IClientCore::IPhase {
public:
    ClientPhase() = default;
    
    [[nodiscard]] bool Wait(milliseconds timeout = 0ms) override {
        std::unique_lock lock(m_mutex);
        return m_cv.wait_for(lock, timeout, [this] { return m_done; });
    }

    void Broadcast() override {
        {
            std::lock_guard lock(m_mutex);
            m_done = true;
        }
        m_cv.notify_all();
    }

    void RunAsync(std::function<void()> action) {
        m_thread = std::jthread([this, action](std::stop_token st) {
            try {
                action();
            } catch (...) {
                kodi::Log(ADDON_LOG_ERROR, "ClientPhase thread error");
            }
            Broadcast();
        });
    }

private:
    std::jthread m_thread;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    bool m_done = false;
};

class ClientCoreBase : public IClientCore {
public:
    explicit ClientCoreBase(RecordingsDelegate recordingsDelegate)
        : m_recordingsDelegate(std::move(recordingsDelegate)) 
    {
        m_phases.try_emplace(Phase::Init, std::make_unique<ClientPhase>());
        // Инициализация остальных фаз
    }

    void InitAsync(bool clearEpgCache, bool updateRecordings) override {
        GetPhase(Phase::Init)->RunAsync([=] {
            InitializeComponents(clearEpgCache);
            ScheduleEpgUpdate();
        });
    }

    const ChannelList& GetChannelList() override {
        auto phase = GetPhase(Phase::ChannelsLoading);
        phase->Wait();
        return m_channels;
    }

    void ReloadRecordings() override {
        std::scoped_lock lock(m_epgMutex);
        UpdateRecordingsInternal();
    }

private:
    struct EpgEntry {
        std::chrono::system_clock::time_point start;
        std::chrono::system_clock::time_point end;
        std::string title;
        std::string description;
    };

    void InitializeComponents(bool clearEpgCache) {
        if (clearEpgCache) {
            ClearEpgCache();
        }
        LoadChannelsAndGroups();
    }

    void ScheduleEpgUpdate() {
        auto now = system_clock::now();
        auto start = now - 7 * 24h;
        auto end = now + 7 * 24h;
        
        m_phases[Phase::EpgLoading]->RunAsync([=] {
            UpdateEpg(start, end);
        });
    }

    void UpdateEpg(system_clock::time_point start, system_clock::time_point end) {
        std::vector<EpgEntry> entries;
        // Загрузка EPG данных
        std::scoped_lock lock(m_epgMutex);
        m_epgEntries.insert(entries.begin(), entries.end());
    }

    void ClearEpgCache() {
        const auto cacheDir = kodi::vfs::TranslateSpecialProtocol(EPG_CACHE_DIR);
        for (const auto& entry : fs::directory_iterator(cacheDir)) {
            if (entry.is_regular_file()) {
                kodi::vfs::DeleteFile(entry.path().string());
            }
        }
    }

    std::shared_ptr<IPhase> GetPhase(Phase phase) const {
        std::shared_lock lock(m_phasesMutex);
        if (auto it = m_phases.find(phase); it != m_phases.end()) {
            return it->second;
        }
        return nullptr;
    }

    // Данные
    std::unordered_map<Phase, std::unique_ptr<ClientPhase>> m_phases;
    std::shared_mutex m_phasesMutex;
    ChannelList m_channels;
    std::mutex m_epgMutex;
    std::unordered_map<int64_t, EpgEntry> m_epgEntries;
    RecordingsDelegate m_recordingsDelegate;
};
} // namespace PvrClient
