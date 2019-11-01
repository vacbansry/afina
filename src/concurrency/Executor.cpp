#include <afina/concurrency/Executor.h>
#include <iostream>
namespace Afina {
namespace Concurrency {

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

Executor::Executor(std::size_t lw, std::size_t hw, std::size_t max_size, std::chrono::milliseconds idle_time)
        : low_watermark(lw), high_watermark(hw), max_queue_size(max_size), idle_time(idle_time) {
    std::lock_guard<std::mutex> _lock(mutex);
    threads.reserve(low_watermark);
    for (int i = 0; i < low_watermark; i++) {
        free_threads++;
        threads.emplace_back(&perform, this);
    }
    state = State::kRun;
}

Executor::~Executor() { Stop(true); }

void Executor::Stop(bool await) {
    std::lock_guard<std::mutex> _lock(mutex);
    state = State::kStopping;
    empty_condition.notify_all();
    if (await) {
        for (auto &t : threads) {
            t.join();
        }
    } else {
        for (auto &t : threads) {
            t.detach();
        }
    }
    state = State::kStopped;
}
} // namespace Concurrency
} // namespace Afinat