#include <afina/concurrency/Executor.h>

namespace Afina {
namespace Concurrency {

void perform(Executor *executor) {
    std::unique_lock<std::mutex> _lock(executor->mutex);
    while (executor->state == Executor::State::kRun) {
        if (!executor->empty_condition.wait_for(_lock, executor->idle_time, [executor] { return !executor->tasks.empty(); })) {
            if (executor->existing_threads > executor->low_watermark) {
                break;
            } else {
                executor->empty_condition.wait(_lock, [executor] { return !executor->tasks.empty() || executor->state != Executor::State::kRun; });
                if (executor->state != Executor::State::kRun) {
                    break;
                }
            }
        }
        executor->free_threads--;
        std::function<void()> task = executor->tasks.front();
        executor->tasks.pop_front();
        _lock.unlock();
        task();
        _lock.lock();
        executor->free_threads++;
    }
    executor->free_threads--;
    executor->existing_threads--;
    if (executor->state == Executor::State::kStopping && executor->existing_threads == 0) {
        executor->state = Executor::State::kStopped;
        executor->_stop_pool.notify_one();
    }
}

Executor::Executor(std::size_t lw, std::size_t hw, std::size_t max_size, std::chrono::milliseconds idle_time)
        : low_watermark(lw), high_watermark(hw), max_queue_size(max_size), idle_time(idle_time) {
    std::unique_lock<std::mutex> _lock(mutex);
    state = State::kRun;
    for (int i = 0; i < low_watermark; i++) {
        std::thread([this] { perform(this); }).detach();
    }
    free_threads = low_watermark;
    existing_threads = low_watermark;
}

Executor::~Executor() { Stop(true); }

void Executor::Stop(bool await) {
    std::unique_lock<std::mutex> _lock(mutex);
    state = State::kStopping;

    if (existing_threads == 0) {
        state = State::kStopped;
    } else {
        empty_condition.notify_all();
        if (await) {
            _stop_pool.wait(_lock, [this] { return this->state == State::kStopped; });
        }
    }
}
} // namespace Concurrency
} // namespace Afinat