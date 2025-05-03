#ifndef SPEEDOMETER_H
#define SPEEDOMETER_H

#include <chrono>
#include <queue>
#include <cstdint>
#include <algorithm>

namespace Helpers {

class Speedometer {
public:
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;
    using Duration = Clock::duration;

    explicit Speedometer(uint64_t dataWindowSize) noexcept
        : m_dataWindowSize(dataWindowSize) {}

    void Reset() noexcept {
        m_steps = {};
        m_totalBytes = 0;
        m_totalSeconds = 0.0;
    }

    void StartMeasurement() noexcept {
        m_currentStart = Clock::now();
    }

    void FinishMeasurement(uint64_t bytesTransferred) noexcept {
        const auto end = Clock::now();
        AddStep({m_currentStart, end, bytesTransferred});
        m_currentStart = end;
    }

    double GetBps() const noexcept {
        return m_totalSeconds > 0.0 ? m_totalBytes / m_totalSeconds : 0.0;
    }

    double GetKBps() const noexcept { return GetBps() / 1024; }
    double GetMBps() const noexcept { return GetKBps() / 1024; }

    uint64_t GetTotalBytes() const noexcept { return m_totalBytes; }
    double GetTotalSeconds() const noexcept { return m_totalSeconds; }

private:
    struct MeasurementStep {
        TimePoint start;
        TimePoint end;
        uint64_t bytes;
        
        double duration() const {
            return std::chrono::duration<double>(end - start).count();
        }
    };

    void AddStep(MeasurementStep&& step) {
        m_steps.push(std::move(step));
        m_totalBytes += step.bytes;
        m_totalSeconds += step.duration();
        
        // Maintain sliding window
        while (m_totalBytes > m_dataWindowSize && !m_steps.empty()) {
            const auto& oldest = m_steps.front();
            m_totalBytes -= oldest.bytes;
            m_totalSeconds -= oldest.duration();
            m_steps.pop();
        }
    }

    std::queue<MeasurementStep> m_steps;
    TimePoint m_currentStart;
    uint64_t m_dataWindowSize;
    uint64_t m_totalBytes = 0;
    double m_totalSeconds = 0.0;
};

} // namespace Helpers

#endif // SPEEDOMETER_H
