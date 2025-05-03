#include <chrono>
#include <filesystem>
#include <memory>
#include <vector>
#include <zlib.h>
#include <kodi/AddonBase.h>
#include <kodi/Filesystem.h>
#include <kodi/addon/pvr/EPG.h>
#include <rapidxml/rapidxml.hpp>
#include "helpers.h"
#include "XmlSaxHandler.h"

namespace fs = std::filesystem;
using namespace std::chrono;

namespace XMLTV {
    constexpr auto CACHE_DIR = "special://temp/pvr-puzzle-tv/XmlTvCache/";
    constexpr auto CHUNK_SIZE = 16384;
    constexpr auto CACHE_TTL = 12h;

    class Inflator {
    public:
        explicit Inflator(std::function<size_t(const char*, size_t)> writer)
            : writer_(std::move(writer)) 
        {
            strm_.zalloc = Z_NULL;
            strm_.zfree = Z_NULL;
            strm_.opaque = Z_NULL;
            inflateInit2(&strm_, 16 + MAX_WBITS);
        }

        bool Process(const char* data, size_t size) {
            strm_.avail_in = static_cast<uInt>(size);
            strm_.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(data));

            do {
                char output[CHUNK_SIZE];
                strm_.avail_out = CHUNK_SIZE;
                strm_.next_out = reinterpret_cast<Bytef*>(output);

                const int ret = inflate(&strm_, Z_NO_FLUSH);
                if (ret == Z_STREAM_ERROR) return false;

                const size_t have = CHUNK_SIZE - strm_.avail_out;
                if (writer_(output, have) != have) return false;

            } while (strm_.avail_out == 0);

            return true;
        }

        ~Inflator() {
            inflateEnd(&strm_);
        }

    private:
        z_stream strm_{};
        std::function<size_t(const char*, size_t)> writer_;
    };

    std::string GetCachePath(const std::string& url) {
        const size_t hash = std::hash<std::string>{}(url);
        return (fs::path(CACHE_DIR) / std::to_string(hash)).string();
    }

    bool LoadData(const std::string& path, std::function<bool(const char*, size_t)> processor) {
        auto file = kodi::vfs::CFile(path);
        if (!file.Open()) return false;

        std::vector<char> buffer(CHUNK_SIZE);
        while (true) {
            const ssize_t read = file.Read(buffer.data(), CHUNK_SIZE);
            if (read <= 0) break;
            if (!processor(buffer.data(), read)) return false;
        }
        return true;
    }

    bool ShouldReloadCache(const fs::path& cached, const fs::path& source) {
        if (!kodi::vfs::FileExists(cached, false)) return true;

        const auto now = system_clock::now();
        const auto cache_time = kodi::vfs::FileStatus(cached).GetModificationTime();
        if (now - cache_time > CACHE_TTL) return true;

        return kodi::vfs::FileStatus(source).GetSize() != kodi::vfs::FileStatus(cached).GetSize();
    }

    bool UpdateCache(const std::string& url, const std::string& cached_path) {
        try {
            kodi::vfs::CreateDirectory(CACHE_DIR);

            auto writer = [&](const char* data, size_t size) {
                static kodi::vfs::CFile file(cached_path, ADDON_WRITE_TRUNCATED);
                return file.Write(data, size) ? size : 0;
            };

            bool is_compressed = false;
            Inflator inflator(writer);

            return LoadData(url, [&](const char* data, size_t size) {
                if (!is_compressed) {
                    is_compressed = (size >= 3 && data[0] == '\x1F' && data[1] == '\x8B' && data[2] == '\x08');
                }
                return is_compressed ? inflator.Process(data, size) : writer(data, size);
            });
        }
        catch (const std::exception& e) {
            kodi::Log(ADDON_LOG_ERROR, "Cache update failed: %s", e.what());
            return false;
        }
    }

    time_t ParseDateTime(std::string_view str) {
        tm tm = {};
        const auto parse_result = std::sscanf(str.data(), 
            "%4d%2d%2d%2d%2d%2d",
            &tm.tm_year, &tm.tm_mon, &tm.tm_mday,
            &tm.tm_hour, &tm.tm_min, &tm.tm_sec
        );

        if (parse_result != 6) {
            throw std::invalid_argument("Invalid datetime format");
        }

        tm.tm_year -= 1900;
        tm.tm_mon -= 1;
        tm.tm_isdst = -1;

        const auto tp = system_clock::from_time_t(std::mktime(&tm));
        return system_clock::to_time_t(tp);
    }

    template<typename T>
    class XmlHandler : public XmlSaxHandler {
    public:
        using Callback = std::function<void(const T&)>;
        
        explicit XmlHandler(Callback cb) : callback_(std::move(cb)) {}

    protected:
        Callback callback_;
        T current_entry_;
        bool in_target_tag_ = false;
    };

    class ChannelHandler : public XmlHandler<EpgChannel> {
    public:
        using XmlHandler::XmlHandler;

        bool StartElement(const std::string& name, const AttributeList& attrs) override {
            if (name == "channel") {
                current_entry_ = EpgChannel();
                if (const auto it = std::find_if(attrs.begin(), attrs.end(),
                    [](const auto& p) { return p.first == "id"; }); it != attrs.end()) 
                {
                    current_entry_.id = std::hash<std::string>{}(it->second);
                    in_target_tag_ = true;
                }
            }
            return true;
        }

        bool EndElement(const std::string& name) override {
            if (name == "channel" && in_target_tag_) {
                callback_(current_entry_);
                in_target_tag_ = false;
            }
            return true;
        }
    };

    bool LoadEPG(const std::string& url, std::function<void(const EpgEntry&)> callback) {
        const auto cached_path = GetCachePath(url);
        
        if (ShouldReloadCache(cached_path, url) && !UpdateCache(url, cached_path)) {
            kodi::Log(ADDON_LOG_ERROR, "Failed to update EPG cache");
            return false;
        }

        try {
            ChannelHandler handler([&](const auto& channel) {
                // Process channel
            });

            return LoadData(cached_path, [&](const char* data, size_t size) {
                return handler.Parse(data, size);
            });
        }
        catch (const std::exception& e) {
            kodi::Log(ADDON_LOG_ERROR, "EPG parsing failed: %s", e.what());
            return false;
        }
    }
}
