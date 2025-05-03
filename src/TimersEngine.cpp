#include <algorithm>
#include <memory>
#include <filesystem>
#include <set>
#include <atomic>
#include <condition_variable>
#include <kodi/addon/pvr/Timers.h>
#include <kodi/Filesystem.h>
#include "TimersEngine.hpp"
#include "globals.hpp"

using namespace std::chrono;
namespace fs = std::filesystem;

namespace Engines {
    // Константы путей
    const fs::path CACHE_DIR = "special://temp/pvr-puzzle-tv/";
    const fs::path CACHE_FILE = CACHE_DIR / "timers.dat";
    
    // Вспомогательная функция для преобразования времени
    static time_t to_time_t(const system_clock::time_point& tp) {
        return system_clock::to_time_t(tp);
    }

    class Timer {
    public:
        explicit Timer(const kodi::addon::PVRTimer& timer) 
            : m_pvrTimer(timer) 
        {
            m_pvrTimer.SetClientIndex(++s_lastIndex);
        }

        system_clock::time_point start_time() const {
            return system_clock::from_time_t(
                m_pvrTimer.GetStartTime() - m_pvrTimer.GetMarginStart() * 60);
        }

        system_clock::time_point end_time() const {
            return system_clock::from_time_t(
                m_pvrTimer.GetEndTime() + m_pvrTimer.GetMarginEnd() * 60);
        }

        void schedule() {
            m_pvrTimer.SetState(PVR_TIMER_STATE_SCHEDULED);
            kodi::Log(ADDON_LOG_DEBUG, "Timer %s scheduled", title().c_str());
        }

        bool start_recording(ITimersEngineDelegate* delegate) {
            bool success = false;
            try {
                success = delegate->StartRecordingFor(m_pvrTimer);
            } 
            catch (const std::exception& e) {
                kodi::Log(ADDON_LOG_ERROR, "Recording error: %s", e.what());
            }
            
            m_pvrTimer.SetState(success ? 
                PVR_TIMER_STATE_RECORDING : 
                PVR_TIMER_STATE_ERROR);
            
            return success;
        }

        bool stop_recording(ITimersEngineDelegate* delegate) {
            const bool success = delegate->StopRecordingFor(m_pvrTimer);
            const auto state = success ? 
                (time(nullptr) >= end_time().time_since_epoch().count() ? 
                    PVR_TIMER_STATE_COMPLETED : 
                    PVR_TIMER_STATE_CANCELLED) : 
                PVR_TIMER_STATE_ERROR;
            
            m_pvrTimer.SetState(state);
            return success;
        }

        const std::string& title() const { 
            return m_pvrTimer.GetTitle(); 
        }

        kodi::addon::PVRTimer m_pvrTimer;

    private:
        inline static std::atomic_uint s_lastIndex = PVR_TIMER_NO_CLIENT_INDEX;
    };

    // Компаратор для умных указателей
    struct TimerCompare {
        bool operator()(const std::unique_ptr<Timer>& a, 
                       const std::unique_ptr<Timer>& b) const {
            return a->start_time() < b->start_time();
        }
    };

    TimersEngine::TimersEngine(ITimersEngineDelegate* delegate)
        : m_delegate(delegate)
        , m_worker([this](std::stop_token st) { process(st); })
    {
        load_cache();
    }

    TimersEngine::~TimersEngine() {
        m_worker.request_stop();
        m_cv.notify_all();
        save_cache();
    }

    void TimersEngine::load_cache() {
        kodi::vfs::CreateDirectory(CACHE_DIR.string());
        
        auto file = kodi::vfs::CFile(CACHE_FILE.string());
        if (!file.Open()) {
            kodi::Log(ADDON_LOG_DEBUG, "No timer cache found");
            return;
        }

        std::lock_guard lock(m_mutex);
        try {
            int32_t count;
            file.Read(&count, sizeof(count));
            
            while (count-- > 0) {
                PVR_TIMER timer;
                file.Read(&timer, sizeof(timer));
                
                auto t = std::make_unique<Timer>(kodi::addon::PVRTimer(timer));
                if (t->m_pvrTimer.GetState() == PVR_TIMER_STATE_RECORDING) {
                    t->m_pvrTimer.SetState(PVR_TIMER_STATE_ABORTED);
                }
                m_timers.insert(std::move(t));
            }
        }
        catch (const std::exception& e) {
            kodi::Log(ADDON_LOG_ERROR, "Cache load error: %s", e.what());
        }
    }

    void TimersEngine::save_cache() const {
        std::lock_guard lock(m_mutex);
        
        try {
            kodi::vfs::CFile file(CACHE_FILE.string(), ADDON_WRITE_TRUNCATED);
            const int32_t count = m_timers.size();
            file.Write(&count, sizeof(count));
            
            for (const auto& timer : m_timers) {
                const auto data = *timer->m_pvrTimer.GetCStructure();
                file.Write(&data, sizeof(data));
            }
        }
        catch (const std::exception& e) {
            kodi::Log(ADDON_LOG_ERROR, "Cache save error: %s", e.what());
            fs::remove(CACHE_FILE);
        }
    }

    void TimersEngine::process(std::stop_token st) {
        while (!st.stop_requested()) {
            std::unique_lock lock(m_mutex);
            const auto now = system_clock::now();
            auto next_wakeup = now + 24h;

            // Обработка таймеров
            for (const auto& timer : m_timers) {
                const auto start = timer->start_time();
                const auto end = timer->end_time();
                
                if (timer->m_pvrTimer.GetState() == PVR_TIMER_STATE_RECORDING) {
                    if (end <= now) {
                        timer->stop_recording(m_delegate);
                    } else {
                        next_wakeup = std::min(next_wakeup, end);
                    }
                }
                else if (timer->m_pvrTimer.GetState() == PVR_TIMER_STATE_SCHEDULED) {
                    if (start <= now) {
                        timer->start_recording(m_delegate);
                        next_wakeup = std::min(next_wakeup, end);
                    } else {
                        next_wakeup = std::min(next_wakeup, start);
                    }
                }
            }

            kodi::addon::CInstancePVRClient::TriggerTimerUpdate();
            
            m_cv.wait_until(lock, next_wakeup, [&] { 
                return st.stop_requested(); 
            });
        }
    }

    PVR_ERROR TimersEngine::AddTimer(const kodi::addon::PVRTimer& timer) {
        std::lock_guard lock(m_mutex);
        m_timers.insert(std::make_unique<Timer>(timer));
        m_cv.notify_all();
        save_cache();
        return PVR_ERROR_NO_ERROR;
    }

    PVR_ERROR TimersEngine::DeleteTimer(const kodi::addon::PVRTimer& timer, bool force) {
        std::lock_guard lock(m_mutex);
        
        const auto it = std::find_if(m_timers.begin(), m_timers.end(),
            [&](const auto& t) { 
                return t->m_pvrTimer.GetClientIndex() == timer.GetClientIndex(); 
            });
        
        if (it != m_timers.end()) {
            if ((*it)->m_pvrTimer.GetState() == PVR_TIMER_STATE_RECORDING && !force) {
                return PVR_ERROR_RECORDING_RUNNING;
            }
            (*it)->stop_recording(m_delegate);
            m_timers.erase(it);
            save_cache();
            return PVR_ERROR_NO_ERROR;
        }
        return PVR_ERROR_INVALID_PARAMETERS;
    }

    PVR_ERROR TimersEngine::GetTimers(kodi::addon::PVRTimersResultSet& results) {
        std::lock_guard lock(m_mutex);
        for (const auto& timer : m_timers) {
            results.Add(timer->m_pvrTimer);
        }
        return PVR_ERROR_NO_ERROR;
    }
}
