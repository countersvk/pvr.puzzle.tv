#ifndef ACTION_QUEUE_HPP
#define ACTION_QUEUE_HPP

#include <kodi/AddonBase.h>       // Kodi 20+ API
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <thread>
#include <exception>
#include <string>
#include <memory>

namespace ActionQueue
{
    class ActionQueueException : public std::exception
    {
    public:
        ActionQueueException(const char* reason = "") : r(reason) {}
        const char* what() const noexcept override { return r.c_str(); }
        
    private:
        std::string r;
    };

    template<typename TAction, typename TCompletion>
    class CActionQueue
    {
    private:
        class QueueItem : public IActionQueueItem
        {
        public:
            QueueItem(TAction action, TCompletion completion)
                : action_(std::move(action)), completion_(std::move(completion)) {}

            void Perform() override 
            {
                try {
                    action_();
                    completion_(ActionResult(kActionCompleted));
                } catch (...) {
                    completion_(ActionResult(kActionFailed, std::current_exception()));
                }
            }

            void Cancel() override 
            {
                completion_(ActionResult(kActionCancelled));
            }

        private:
            TAction action_;
            TCompletion completion_;
        };

    public:
        CActionQueue(size_t maxSize, const char* name = "")
            : max_size_(maxSize), name_(name), running_(false) {}

        ~CActionQueue()
        {
            StopThread(5000);
        }

        void PerformHiPriority(TAction action, TCompletion completion)
        {
            std::unique_lock<std::mutex> lock(priority_mutex_);
            if (priority_action_) {
                throw ActionQueueException("Too many priority tasks");
            }

            auto item = std::make_unique<QueueItem>(std::move(action), std::move(completion));
            priority_action_ = std::move(item);

            // Wakeup worker thread
            {
                std::lock_guard<std::mutex> qlock(queue_mutex_);
                queue_cond_.notify_one();
            }

            priority_cond_.wait(lock, [this] { return !priority_action_; });
        }

        void PerformAsync(TAction action, TCompletion completion)
        {
            if (will_stop_) return;

            std::unique_ptr<QueueItem> item = std::make_unique<QueueItem>(std::move(action), std::move(completion));
            
            {
                std::lock_guard<std::mutex> lock(queue_mutex_);
                if (queue_.size() >= max_size_) {
                    throw ActionQueueException("Queue overflow");
                }
                queue_.push(std::move(item));
            }
            queue_cond_.notify_one();
        }

        bool StopThread(int waitMs = 5000)
        {
            will_stop_ = true;
            queue_cond_.notify_all();

            if (worker_.joinable()) {
                if (waitMs > 0) {
                    auto timeout = std::chrono::steady_clock::now() + 
                                  std::chrono::milliseconds(waitMs);
                    if (worker_.join_until(timeout)) {
                        worker_.join();
                        return true;
                    }
                }
                worker_.detach();
                return false;
            }
            return true;
        }

        void Start()
        {
            if (!running_) {
                running_ = true;
                worker_ = std::thread(&CActionQueue::Process, this);
            }
        }

    private:
        void Process()
        {
            kodi::addon::SetThreadName(GetCurrentThread(), name_.c_str());

            while (running_) 
            {
                std::unique_ptr<QueueItem> item;
                
                // Check priority action first
                {
                    std::lock_guard<std::mutex> lock(priority_mutex_);
                    if (priority_action_) {
                        item = std::move(priority_action_);
                    }
                }

                if (!item) {
                    std::unique_lock<std::mutex> lock(queue_mutex_);
                    queue_cond_.wait(lock, [this] { 
                        return !queue_.empty() || !running_; 
                    });

                    if (!queue_.empty()) {
                        item = std::move(queue_.front());
                        queue_.pop();
                    }
                }

                if (item) {
                    try {
                        if (will_stop_) item->Cancel();
                        else item->Perform();
                    } catch (...) {
                        kodi::Log(ADDON_LOG_ERROR, "Unhandled exception in action queue");
                    }
                }

                // Notify priority completion
                if (priority_action_) {
                    std::lock_guard<std::mutex> lock(priority_mutex_);
                    priority_action_.reset();
                    priority_cond_.notify_one();
                }
            }
        }

        const size_t max_size_;
        std::string name_;
        std::atomic_bool running_;
        std::atomic_bool will_stop_{false};
        
        std::mutex queue_mutex_;
        std::condition_variable queue_cond_;
        std::queue<std::unique_ptr<QueueItem>> queue_;

        std::mutex priority_mutex_;
        std::condition_variable priority_cond_;
        std::unique_ptr<QueueItem> priority_action_;

        std::thread worker_;
    };
}

#endif // ACTION_QUEUE_HPP
