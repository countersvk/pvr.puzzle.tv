#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <vector>
#include <queue>
#include <memory>
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <future>
#include <functional>
#include <type_traits>
#include <stop_token>
#include <cassert>

namespace modern {

class ThreadPool {
public:
    explicit ThreadPool(size_t threads = std::max(2u, std::thread::hardware_concurrency()));
    ~ThreadPool();

    template<class F, class... Args>
    auto enqueue(F&& f, Args&&... args)
        -> std::future<std::invoke_result_t<F, Args...>>;

    void wait_idle() noexcept;
    void resize(size_t new_size);
    void set_queue_limit(size_t limit) noexcept;

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

private:
    struct Task {
        std::function<void()> func;
        std::stop_token stop_token;
    };

    void worker_main(std::stop_token st);

    std::vector<std::jthread> workers_;
    std::queue<Task> tasks_;
    std::mutex queue_mutex_;
    std::condition_variable_any queue_cv_;
    
    std::atomic<size_t> tasks_in_flight_{0};
    std::condition_variable done_cv_;
    
    size_t queue_limit_ = 100'000;
    std::atomic_bool stop_{false};
};

// Implementation

inline ThreadPool::ThreadPool(size_t threads) {
    workers_.reserve(threads);
    while (workers_.size() < threads) {
        workers_.emplace_back([this](std::stop_token st) { worker_main(st); });
    }
}

inline ThreadPool::~ThreadPool() {
    stop_.store(true, std::memory_order_release);
    queue_cv_.notify_all();
}

template<class F, class... Args>
auto ThreadPool::enqueue(F&& f, Args&&... args)
    -> std::future<std::invoke_result_t<F, Args...>> 
{
    using return_type = std::invoke_result_t<F, Args...>;
    
    if (stop_.load(std::memory_order_acquire)) {
        throw std::runtime_error("enqueue on stopped ThreadPool");
    }

    auto task = std::make_shared<std::packaged_task<return_type()>>(
        std::bind(std::forward<F>(f), std::forward<Args>(args)...)
    );

    std::future<return_type> res = task->get_future();
    
    {
        std::scoped_lock lock(queue_mutex_);
        
        if (tasks_.size() >= queue_limit_) {
            queue_cv_.wait(lock, [this] {
                return tasks_.size() < queue_limit_ || stop_;
            });
        }

        if (stop_) return res;

        tasks_.emplace(Task{
            [task](){ (*task)(); }, 
            workers_.front().get_stop_token()
        });
        
        tasks_in_flight_.fetch_add(1, std::memory_order_relaxed);
    }

    queue_cv_.notify_one();
    return res;
}

inline void ThreadPool::worker_main(std::stop_token st) {
    while (!st.stop_requested()) {
        Task task;
        
        {
            std::unique_lock lock(queue_mutex_);
            queue_cv_.wait(lock, [this, &st] {
                return !tasks_.empty() || st.stop_requested();
            });

            if (st.stop_requested() || tasks_.empty()) break;

            task = std::move(tasks_.front());
            tasks_.pop();
        }

        try {
            task.func();
        } catch (...) {
            // Log exception here if needed
        }

        if (tasks_in_flight_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
            done_cv_.notify_all();
        }
    }
}

inline void ThreadPool::wait_idle() noexcept {
    std::unique_lock lock(queue_mutex_);
    done_cv_.wait(lock, [this] {
        return tasks_in_flight_.load(std::memory_order_acquire) == 0;
    });
}

inline void ThreadPool::resize(size_t new_size) {
    if (new_size < 1) new_size = 1;
    
    std::scoped_lock lock(queue_mutex_);
    const size_t current = workers_.size();
    
    if (new_size > current) {
        workers_.reserve(new_size);
        while (workers_.size() < new_size) {
            workers_.emplace_back([this](std::stop_token st) { worker_main(st); });
        }
    } else if (new_size < current) {
        for (size_t i = 0; i < current - new_size; ++i) {
            workers_[i].request_stop();
        }
        queue_cv_.notify_all();
    }
}

inline void ThreadPool::set_queue_limit(size_t limit) noexcept {
    queue_limit_ = std::max(limit, size_t{1});
}

} // namespace modern

#endif // THREAD_POOL_H
