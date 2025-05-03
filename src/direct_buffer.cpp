#include <chrono>
#include <filesystem>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>
#include <kodi/AddonBase.h>
#include <kodi/Filesystem.h>

namespace fs = std::filesystem;
using namespace std::chrono_literals;

namespace Buffers {

class DirectBuffer {
public:
    explicit DirectBuffer(const fs::path& stream_url)
        : m_handle(kodi::vfs::CFile(stream_url, 
            ADDON_READ_AUDIO_VIDEO | ADDON_READ_TRUNCATED | 
            ADDON_READ_CHUNKED | ADDON_READ_BITRATE | ADDON_READ_CACHED))
    {
        if (!m_handle.Open())
            throw std::runtime_error("Failed to open stream");
    }

    virtual ~DirectBuffer() = default;

    [[nodiscard]] int64_t length() const {
        return m_handle.GetLength();
    }

    [[nodiscard]] int64_t position() const {
        return m_handle.GetPosition();
    }

    [[nodiscard]] ssize_t read(std::span<uint8_t> buffer, milliseconds timeout = 0ms) {
        std::unique_lock lock(m_mutex, std::defer_lock);
        if (!lock.try_lock_for(timeout))
            return -1;

        return m_handle.Read(buffer.data(), buffer.size());
    }

    [[nodiscard]] int64_t seek(int64_t position, int whence) {
        return m_handle.Seek(position, whence);
    }

    bool switch_stream(const fs::path& new_url) {
        std::lock_guard lock(m_mutex);
        m_handle.Close();
        return m_handle.Open(new_url);
    }

protected:
    kodi::vfs::CFile m_handle;
    mutable std::timed_mutex m_mutex;
};

class ArchiveBuffer : public DirectBuffer {
public:
    using DirectBuffer::DirectBuffer;

    ~ArchiveBuffer() override = default;

    [[nodiscard]] int64_t length() const override {
        return m_handle.GetLength();
    }

    [[nodiscard]] int64_t position() const override {
        return m_handle.GetPosition();
    }

    [[nodiscard]] int64_t seek(int64_t position, int whence) override {
        return m_handle.Seek(position, whence);
    }
};

} // namespace Buffers
