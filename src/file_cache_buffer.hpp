#ifndef FILE_CACHE_BUFFER_HPP
#define FILE_CACHE_BUFFER_HPP

#include <memory>
#include <string>
#include <vector>
#include <deque>
#include <mutex>
#include <filesystem>
#include "cache_buffer.h"

namespace Buffers {

class CAddonFile;

class FileCacheBuffer : public ICacheBuffer {
public:
    static constexpr uint32_t STREAM_READ_BUFFER_SIZE = 32768; // 32KB
    static constexpr uint32_t CHUNK_FILE_SIZE_LIMIT = STREAM_READ_BUFFER_SIZE * 4096; // 128MB

    // Read-Write constructor
    FileCacheBuffer(std::filesystem::path bufferCacheDir, uint8_t sizeFactor, bool autoDelete = true);
    
    // Read-Only constructor
    explicit FileCacheBuffer(std::filesystem::path bufferCacheDir);
    
    virtual ~FileCacheBuffer() override;

    // ICacheBuffer interface implementation
    void Init() override;
    uint32_t UnitSize() noexcept override;
    
    int64_t Seek(int64_t iFilePosition, int iWhence) override;
    int64_t Length() noexcept override;
    int64_t Position() noexcept override;
    ssize_t Read(void* lpBuf, size_t uiBufSize) override;
    
    bool LockUnitForWrite(uint8_t** pBuf) override;
    void UnlockAfterWriten(uint8_t* pBuf, ssize_t writtenBytes = -1) override;

    time_t StartTime() const noexcept override { return m_startTime; }
    time_t EndTime() const noexcept override { return m_endTime; }
    float FillingRatio() const noexcept override { 
        return static_cast<float>(m_position - m_begin) / (m_length - m_begin); 
    }

    // Delete copy operations
    FileCacheBuffer(const FileCacheBuffer&) = delete;
    FileCacheBuffer& operator=(const FileCacheBuffer&) = delete;

private:
    using ChunkFilePtr = std::unique_ptr<CAddonFile>;
    using FileChunks = std::deque<ChunkFilePtr>;
    using ChunkFileSwarm = std::deque<ChunkFilePtr>;

    ChunkFilePtr CreateChunk();
    std::pair<size_t, int64_t> LocateChunk(int64_t position) const;
    
    // Member variables
    mutable std::mutex m_syncAccess;
    FileChunks m_readChunks;
    ChunkFileSwarm m_chunkFileSwarm;
    
    std::filesystem::path m_bufferDir;
    const int64_t m_maxSize;
    const bool m_autoDelete;
    const bool m_isReadOnly;
    
    int64_t m_length = 0;
    int64_t m_position = 0;
    int64_t m_begin = 0; // Virtual start of cache
    
    std::vector<uint8_t> m_chunkBuffer;
    time_t m_startTime = 0;
    time_t m_endTime = 0;
};

} // namespace Buffers

#endif // FILE_CACHE_BUFFER_HPP
