#ifndef XMLTV_LOADER_HPP
#define XMLTV_LOADER_HPP

#include <string>
#include <vector>
#include <functional>
#include <chrono>
#include <filesystem>
#include <memory>
#include <kodi/AddonBase.h>
#include <kodi/Filesystem.h>
#include <kodi/addon/pvr/EPG.h>

namespace XMLTV {
    namespace fs = std::filesystem;
    using namespace std::chrono;

    struct EpgChannel {
        EpgChannel() = default;
        
        kodi::addon::PVRChannelIdentifier id = KODI_INVALID_CHANNEL_ID;
        std::vector<std::string> displayNames;
        std::string iconPath;
    };

    struct EpgEntry {
        kodi::addon::PVRChannelIdentifier channelId = KODI_INVALID_CHANNEL_ID;
        time_point<system_clock> startTime;
        time_point<system_clock> endTime;
        std::string title;
        std::string plot;
        std::string genre;
        std::string iconPath;
    };

    using ChannelHandler = std::function<void(const EpgChannel&)>;
    using EpgEntryHandler = std::function<bool(const EpgEntry&)>;

    class XmlTvLoader {
    public:
        explicit XmlTvLoader(std::string cacheDir = "special://temp/pvr-xmltv/")
            : m_cacheDir(std::move(cacheDir)) 
        {
            kodi::vfs::CreateDirectory(m_cacheDir);
        }

        bool LoadChannels(const std::string& source, ChannelHandler handler);
        bool LoadPrograms(const std::string& source, EpgEntryHandler handler);

        static time_point<system_clock> ParseXmlDateTime(std::string_view dtStr);

    private:
        struct CacheInfo {
            fs::path path;
            bool valid = false;
            system_clock::time_point expiration;
        };

        fs::path m_cacheDir;
        
        CacheInfo GetCachedData(const std::string& source);
        bool UpdateCache(const std::string& source, const fs::path& cachePath);
        bool ProcessXml(const fs::path& xmlPath, ChannelHandler* channelHandler, EpgEntryHandler* programHandler);
    };

    // Хэш-функция для идентификаторов каналов
    struct ChannelIdHash {
        size_t operator()(const std::string& id) const {
            return std::hash<std::string>{}(id);
        }
    };

    // Вспомогательные функции времени
    system_clock::time_point LocalTimeToUTC(const tm& localTime);
    time_t LocalTimeOffset();
}

#endif // XMLTV_LOADER_HPP
