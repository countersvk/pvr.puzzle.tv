#include <memory>
#include <string_view>
#include <format>
#include <kodi/AddonBase.h>
#include <kodi/Filesystem.h>

namespace Globals {
    // Singleton-менеджер для глобальных ресурсов
    class AddonManager {
    public:
        static AddonManager& instance() {
            static AddonManager instance;
            return instance;
        }

        void init(IAddonDelegate* pvr) {
            std::lock_guard lock(mutex_);
            pvr_ = pvr;
        }

        void cleanup() {
            std::lock_guard lock(mutex_);
            pvr_ = nullptr;
        }

        IAddonDelegate* pvr() const {
            return pvr_;
        }

        AddonManager(const AddonManager&) = delete;
        AddonManager& operator=(const AddonManager&) = delete;

    private:
        AddonManager() = default;
        ~AddonManager() = default;

        IAddonDelegate* pvr_ = nullptr;
        mutable std::mutex mutex_;
    };

    // Функции логирования с variadic templates
    template<typename... Args>
    void LogFatal(std::format_string<Args...> fmt, Args&&... args) {
        kodi::Log(ADDON_LOG_FATAL, std::format(fmt, std::forward<Args>(args)....c_str());
    }

    template<typename... Args>
    void LogError(std::format_string<Args...> fmt, Args&&... args) {
        kodi::Log(ADDON_LOG_ERROR, std::format(fmt, std::forward<Args>(args)...).c_str());
    }

    template<typename... Args>
    void LogInfo(std::format_string<Args...> fmt, Args&&... args) {
        kodi::Log(ADDON_LOG_INFO, std::format(fmt, std::forward<Args>(args)...).c_str());
    }

    template<typename... Args>
    void LogNotice(std::format_string<Args...> fmt, Args&&... args) {
        kodi::Log(ADDON_LOG_WARNING, std::format(fmt, std::forward<Args>(args)...).c_str());
    }

    template<typename... Args>
    void LogDebug(std::format_string<Args...> fmt, Args&&... args) {
        kodi::Log(ADDON_LOG_DEBUG, std::format(fmt, std::forward<Args>(args)...).c_str());
    }

    // Функция для безопасного открытия файлов
    std::unique_ptr<kodi::vfs::CFile> OpenFile(const std::string& path, unsigned int flags) {
        auto file = std::make_unique<kodi::vfs::CFile>();
        if (file->OpenFile(path, flags)) {
            return file;
        }
        return nullptr;
    }
}
