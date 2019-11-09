#ifndef AFINA_CONCURRENCY_EXECUTOR_H
#define AFINA_CONCURRENCY_EXECUTOR_H

#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <chrono>
#include <algorithm>
#include <iostream>

namespace Afina {
namespace Concurrency {

/**
* # Thread pool
*/
class Executor {
public:
    enum class State {
        // Threadpool is fully operational, tasks could be added and get executed
                kRun,

        // Threadpool is on the way to be shutdown, no ned task could be added, but existing will be
        // completed as requested
                kStopping,

        // Threadpool is stopped
                kStopped
    };

    Executor(std::size_t lw, std::size_t hw, std::size_t max_size, std::chrono::milliseconds idle_time);

    ~Executor();

    /**
     * Signal thread pool to stop, it will stop accepting new jobs and close threads just after each become
     * free. All enqueued jobs will be complete.
     *
     * In case if await flag is true, call won't return until all background jobs are done and all threads are stopped
     */
    void Stop(bool await = false);

    /**
     * Add function to be executed on the threadpool. Method returns true in case if task has been placed
     * onto execution queue, i.e scheduled for execution and false otherwise.
     *
     * That function doesn't wait for function result. Function could always be written in a way to notify caller about
     * execution finished by itself
     */
    template <typename F, typename... Types> bool Execute(F &&func, Types... args) {
        // Prepare "task"
        auto exec = std::bind(std::forward<F>(func), std::forward<Types>(args)...);
        std::unique_lock<std::mutex> lock(this->mutex);
        if (state != State::kRun || current_queue_size == max_queue_size) {
            return false;
        }

        // Enqueue new task
        current_queue_size++;
        tasks.push_back(exec);
        if (free_threads == 0 && threads.size() < high_watermark) {
            {
                std::lock_guard<std::mutex> _lock(mutex);
                free_threads++;
                threads.emplace_back(std::thread([this] { perform(this); }));
                threads.back().detach();
            }
        } else if (threads.size() < high_watermark) {
            empty_condition.notify_one();
        }
        return true;
    }

private:
    // No copy/move/assign allowed
    Executor(const Executor &);            // = delete;
    Executor(Executor &&);                 // = delete;
    Executor &operator=(const Executor &); // = delete;
    Executor &operator=(Executor &&);      // = delete;

    /**
     * Main function that all pool threads are running. It polls internal task queue and execute tasks
     */
    friend void perform(Executor *executor);

    std::vector<std::thread>::iterator find_thread() {
        std::thread::id thread_id = std::this_thread::get_id();
        auto it = std::find_if(threads.begin(), threads.end(),
                               [thread_id](std::thread &t) { return (t.get_id() == thread_id); });
        return it;
    }

    /**
     * Mutex to protect state below from concurrent modification
     */
    std::mutex mutex;

    /**
     * Conditional variable to await new data in case of empty queue
     */
    std::condition_variable empty_condition;
    /**
     * Vector of actual threads that perform execution
     */
    std::vector<std::thread> threads;

    /**
     * Task queue
     */
    std::deque<std::function<void()>> tasks;

    /**
     * Flag to stop bg threads
     */
    State state;

    std::size_t  low_watermark;
    std::size_t  high_watermark;
    std::size_t  max_queue_size;
    std::chrono::milliseconds idle_time;

    std::condition_variable _stop_pool;

    std::size_t free_threads;
    std::size_t current_queue_size = 0;

    std::string name;
    std::size_t size;
};

} // namespace Concurrency
} // namespace Afina

#endif // AFINA_CONCURRENCY_EXECUTOR_H
