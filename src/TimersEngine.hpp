#ifndef TIMERS_ENGINE_H
#define TIMERS_ENGINE_H

#include <atomic>
#include <set>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <kodi/addon/pvr/Timers.h>
#include "ITimersEngine.h"

namespace Engines {

class Timer;
class ITimersEngineDelegate;

class TimersEngine : public ITimersEngine
{
public:
    explicit TimersEngine(ITimersEngineDelegate* delegate);
    ~TimersEngine() override;

    // Запрет копирования и присваивания
    TimersEngine(const TimersEngine&) = delete;
    TimersEngine& operator=(const TimersEngine&) = delete;

    int GetTimersAmount() override;
    PVR_ERROR AddTimer(const kodi::addon::PVRTimer& timer) override;
    PVR_ERROR GetTimers(kodi::addon::PVRTimersResultSet& results) override;
    PVR_ERROR DeleteTimer(const kodi::addon::PVRTimer& timer, bool forceDelete) override;
    PVR_ERROR UpdateTimer(const kodi::addon::PVRTimer& timer) override;

private:
    struct TimerCompare {
        bool operator()(const std::unique_ptr<Timer>& a, 
                       const std::unique_ptr<Timer>& b) const;
    };

    using TimersSet = std::set<std::unique_ptr<Timer>, TimerCompare>;

    void WorkerMain(std::stop_token stopToken);
    void LoadCache();
    void SaveCache() const;

    mutable std::mutex m_mutex;
    std::condition_variable_any m_cv;
    TimersSet m_timers;
    ITimersEngineDelegate* m_delegate;
    std::jthread m_worker;
    std::atomic_bool m_cacheModified{false};
};

} // namespace Engines

#endif // TIMERS_ENGINE_H
