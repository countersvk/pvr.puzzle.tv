#ifndef GLOBALS_HPP
#define GLOBALS_HPP

#include <memory>
#include <string_view>
#include <mutex>
#include <kodi/AddonBase.h>
#include <kodi/Filesystem.h>

namespace Globals {

class AddonManager {
public:
    static AddonManager& instance() {
        static AddonManager instance;
        return instance;
    }

    void init(kodi::addon::CInstancePVRClient* pvr) {
        std::lock_guard lock(mutex_);
        pvr_instance_ = pvr;
    }

    void cleanup() {
        std::lock_guard lock(mutex_);
        pvr_instance_ = nullptr;
    }

    kodi::addon::CInstancePVRClient* pvr() const {
        return pvr_instance_;
    }

    AddonManager(const AddonManager&) = delete;
    AddonManager& operator=(const AddonManager&) = delete;

private:
    AddonManager() = default;
    ~AddonManager() = default;

    kodi::addon::CInstancePVRClient* pvr_instance_ = nullptr;
    mutable std::mutex mutex_;
};

template<typename... Args>
void LogError(std::string_view format, Args&&... args) {
    kodi::Log(ADDON_LOG_ERROR, kodi::tools::StringUtils::Format(format, std::forward<Args>(args)...));
}

template<typename... Args>
void LogInfo(std::string_view format, Args&&... args) {
    kodi::Log(ADDON_LOG_INFO, kodi::tools::StringUtils::Format(format, std::forward<Args>(args)...));
}

template<typename... Args>
void LogNotice(std::string_view format, Args&&... args) {
    kodi::Log(ADDON_LOG_WARNING, kodi::tools::StringUtils::Format(format, std::forward<Args>(args)...));
}

template<typename... Args>
void LogDebug(std::string_view format, Args&&... args) {
    kodi::Log(ADDON_LOG_DEBUG, kodi::tools::StringUtils::Format(format, std::forward<Args>(args)...));
}

std::unique_ptr<kodi::vfs::CFile> OpenFile(std::string_view path, unsigned int flags = 0);

} // namespace Globals

#endif // GLOBALS_HPP
