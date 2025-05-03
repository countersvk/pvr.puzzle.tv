#pragma once

#include <cstdint>
#include <stdexcept>
#include <memory>
#include <chrono>
#include <system_error>

namespace Buffers {

    enum class SeekOrigin {
        Begin,
        Current,
        End
    };

    class ICacheBuffer {
    public:
        using Clock = std::chrono::system_clock;
        using TimePoint = Clock::time_point;
        using Duration = Clock::duration;

        ICacheBuffer() = default;
        virtual ~ICacheBuffer() = default;

        // Чтение данных
        [[nodiscard]] virtual std::int64_t Seek(std::int64_t offset, SeekOrigin origin) = 0;
        [[nodiscard]] virtual std::int64_t Length() const noexcept = 0;
        [[nodiscard]] virtual std::int64_t Position() const noexcept = 0;
        [[nodiscard]] virtual std::size_t Read(std::byte* buffer, std::size_t size) = 0;

        // Запись данных
        [[nodiscard]] virtual std::span<std::byte> LockForWrite() = 0;
        virtual void CommitWrite(std::size_t bytes_written) = 0;

        // Метаданные
        [[nodiscard]] virtual TimePoint StartTime() const noexcept = 0;
        [[nodiscard]] virtual TimePoint EndTime() const noexcept = 0;
        [[nodiscard]] virtual float FillRatio() const noexcept = 0;

        // Удаление копирования
        ICacheBuffer(const ICacheBuffer&) = delete;
        ICacheBuffer& operator=(const ICacheBuffer&) = delete;

        // Поддержка перемещения
        ICacheBuffer(ICacheBuffer&&) noexcept = default;
        ICacheBuffer& operator=(ICacheBuffer&&) noexcept = default;
    };

    class CacheBufferError : public std::runtime_error {
    public:
        using std::runtime_error::runtime_error;
        
        explicit CacheBufferError(std::error_code ec)
            : std::runtime_error(ec.message()), code(ec) {}

        [[nodiscard]] std::error_code errorCode() const noexcept { return code; }

    private:
        std::error_code code;
    };

} // namespace Buffers
