#include <afina/concurrency/Executor.h>

namespace Afina {
namespace Concurrency {

    Executor::Executor(std::size_t lw, std::size_t hw,
            std::size_t max_size, std::chrono::milliseconds idle_time)
        : low_watermark(lw), high_watermark(hw), max_queue_size(max_size), idle_time(idle_time) {
        std::lock_guard<std::mutex> _lock(mutex);
        while (threads.size() < low_watermark) {
            free_threads++;
            threads.emplace_back(std::thread([this] { perform(this); }));
            threads.back().detach();
        }
        state = State::kRun;
    }

    Executor::~Executor() {
        Stop(true);
    }

    void Executor::Stop(bool await) {
        std::lock_guard<std::mutex> _lock(mutex);
        state = State::kStopping;
        empty_condition.notify_all();
        if (await) {
            std::unique_lock<std::mutex> _lock(_stop_lock);
            _stop_pool.wait(_lock, [this] { return threads.empty(); });
        }
        state = State::kStopped;
    }

    void perform(Executor *executor) {
        while (executor->state == Executor::State::kRun) {
            {
                std::lock_guard<std::mutex> _lock(executor->mutex);
                executor->free_threads--;
            }
            std::unique_lock<std::mutex> _lock(executor->mutex);
            if (!executor->empty_condition.wait_for(_lock, executor->idle_time,
                                                    [executor] { return !executor->tasks.empty(); })) {
                if (executor->threads.size() > executor->low_watermark) {
                    std::lock_guard<std::mutex> _lock(executor->mutex);
                    executor->free_threads--;
                    auto it = executor->find_thread();
                    executor->threads.erase(it);
                    return;
                } else {
                    {
                        std::lock_guard<std::mutex> _lock(executor->mutex);
                        executor->free_threads++;
                    }
                    executor->empty_condition.wait(_lock, [executor] { return !executor->tasks.empty(); });
                }
            } else {
                std::function<void()> task = executor->tasks.front();
                executor->tasks.pop_front();
                std::thread([task] { task(); }).detach();
            }
        }
        {
            std::lock_guard<std::mutex> _lock(executor->mutex);
            auto it = executor->find_thread();
            executor->threads.erase(it);
        }
        if (executor->threads.empty()) {
            executor->_stop_pool.notify_all();
        }
    }
}
} // namespace Afina