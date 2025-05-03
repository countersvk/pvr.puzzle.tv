// direct_buffer.h
#pragma once

#include <memory>
#include <string>
#include <atomic>
#include <mutex>
#include <chrono>
#include <kodi/Filesystem.h>
#include "input_buffer.h"

namespace Buffers {

class ICacheBuffer;

class DirectBuffer : public InputBuffer {
public:
    explicit DirectBuffer(std::string_view stream_url);
    explicit DirectBuffer(std::unique_ptr<ICacheBuffer> cache_buffer);
    virtual ~DirectBuffer() = default;

    const std::string& url() const noexcept { return m_url; }
    
    int64_t length() const noexcept override;
    int64_t position() const noexcept override;
    int64_t seek(int64_t position, int whence) override;
    ssize_t read(unsigned char* buffer, size_t buffer_size, std::chrono::milliseconds timeout) override;
    
    bool switch_stream(std::string_view new_url);
    void abort_read() noexcept;

protected:
    explicit DirectBuffer() = default;

private:
    std::unique_ptr<kodi::vfs::CFile> m_stream;
    std::unique_ptr<ICacheBuffer> m_cache;
    std::string m_url;
    std::atomic<bool> m_abort_read{false};
    mutable std::mutex m_mutex;
};

class ArchiveBuffer final : public DirectBuffer {
public:
    explicit ArchiveBuffer(std::string_view stream_url);
    
    int64_t length() const noexcept override;
    int64_t position() const noexcept override;
    int64_t seek(int64_t position, int whence) override;
};

} // namespace Buffers
