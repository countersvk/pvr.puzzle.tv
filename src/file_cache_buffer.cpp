#include <memory>
#include <filesystem>
#include <chrono>
#include <vector>
#include <algorithm>
#include <mutex>
#include <kodi/Filesystem.h>
#include <stdexcept>

namespace fs = std::filesystem;
namespace chrono = std::chrono;
using namespace std::chrono_literals;

namespace Buffers {

class CacheBufferException : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class CAddonFile {
public:
    CAddonFile(fs::path path, bool auto_delete)
        : path_(std::move(path)),
          writer_(path_, kodi::vfs::OPEN_FLAG_WRITE),
          reader_(path_, kodi::vfs::OPEN_FLAG_READ),
          auto_delete_(auto_delete) {}

    const fs::path& path() const noexcept { return path_; }
    void reopen() {
        writer_.Close();
        reader_.Close();
        writer_.Open(path_);
        reader_.Open(path_);
    }

    ~CAddonFile() {
        writer_.Close();
        reader_.Close();
        if(auto_delete_) {
            kodi::vfs::DeleteFile(path_.string());
        }
    }

private:
    fs::path path_;
    kodi::vfs::CFile writer_;
    kodi::vfs::CFile reader_;
    bool auto_delete_;
};

class FileCacheBuffer {
public:
    FileCacheBuffer(fs::path cache_dir, size_t max_size, bool auto_delete)
        : dir_(std::move(cache_dir)),
          max_size_(max_size),
          auto_delete_(auto_delete),
          chunk_buffer_(std::make_unique<uint8_t[]>(STREAM_READ_BUFFER_SIZE)) 
    {
        if(!fs::exists(dir_) && !kodi::vfs::CreateDirectory(dir_.string())) {
            throw CacheBufferException("Failed to create cache directory");
        }
        initialize();
    }

    size_t unit_size() const noexcept { return STREAM_READ_BUFFER_SIZE; }

    int64_t seek(int64_t position, int whence) {
        std::lock_guard lock(mutex_);
        
        const auto new_pos = calculate_new_position(position, whence);
        const auto [chunk_idx, chunk_pos] = locate_chunk(new_pos);
        
        if(chunk_idx >= chunks_.size()) {
            throw CacheBufferException("Invalid chunk index");
        }

        auto& chunk = chunks_[chunk_idx];
        chunk->reader.Seek(chunk_pos, SEEK_SET);
        current_pos_ = new_pos;
        
        return current_pos_;
    }

    ssize_t read(void* buffer, size_t size) {
        std::lock_guard lock(mutex_);
        return read_impl(static_cast<uint8_t*>(buffer), size);
    }

private:
    struct Chunk {
        std::unique_ptr<CAddonFile> file;
        int64_t start;
        int64_t end;
    };

    void initialize() {
        std::vector<kodi::vfs::CDirEntry> files;
        if(kodi::vfs::GetDirectory(dir_.string(), "*.bin", files)) {
            std::sort(files.begin(), files.end(), [](auto& a, auto& b) {
                return alphanum_comp(a.Path(), b.Path()) < 0;
            });

            for(auto& f : files) {
                add_chunk(fs::path(f.Path()));
            }
        }
    }

    void add_chunk(fs::path path) {
        auto chunk = std::make_unique<CAddonFile>(path, auto_delete_);
        chunks_.push_back({
            std::move(chunk),
            current_size_,
            current_size_ + chunk->size()
        });
        current_size_ += chunk->size();
    }

    std::pair<size_t, int64_t> locate_chunk(int64_t position) const {
        auto it = std::lower_bound(chunks_.begin(), chunks_.end(), position,
            [](const Chunk& c, int64_t pos) { return c.end < pos; });
        
        const size_t idx = std::distance(chunks_.begin(), it);
        const int64_t chunk_pos = position - it->start;
        return {idx, chunk_pos};
    }

    ssize_t read_impl(uint8_t* buffer, size_t size) {
        size_t total_read = 0;
        while(total_read < size) {
            auto [idx, pos] = locate_chunk(current_pos_);
            if(idx >= chunks_.size()) break;

            auto& chunk = chunks_[idx];
            const size_t to_read = std::min(size - total_read, 
                static_cast<size_t>(chunk->end - current_pos_));
            
            const ssize_t read = chunk->file->reader.Read(buffer + total_read, to_read);
            if(read <= 0) break;

            total_read += read;
            current_pos_ += read;
        }
        return total_read;
    }

    fs::path dir_;
    size_t max_size_;
    bool auto_delete_;
    std::vector<Chunk> chunks_;
    int64_t current_pos_ = 0;
    int64_t current_size_ = 0;
    std::unique_ptr<uint8_t[]> chunk_buffer_;
    mutable std::mutex mutex_;
};

} // namespace Buffers
