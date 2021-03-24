#ifndef THREAD_TOOLS_
#define THREAD_TOOLS_

#include <atomic>
#include <bits/stdc++.h>
#include <vector>
#include <queue>
#include <atomic>
#include <future>
#include <condition_variable>
#include <thread>
#include <functional>
#include <stdexcept>

namespace xp
{
    class SpinLock
    {
    public:
        SpinLock() noexcept = default;
        SpinLock(SpinLock &&) noexcept = default;
        SpinLock &operator=(SpinLock &&) noexcept = default;
        SpinLock(const SpinLock &) = delete;
        SpinLock &operator=(const SpinLock &) = delete;
        void lock() noexcept
        {
            while (flag_.test_and_set(std::memory_order::acquire))
                continue;
        }
        void unlock() noexcept
        {
            flag_.clear(std::memory_order::release);
        }
        bool try_lock() noexcept
        {
            return flag_.test_and_set(std::memory_order::acquire);
        }

    private:
        std::atomic_flag flag_;
    };

    class ThreadPool
    {
    public:
        ThreadPool(size_t thread_num = 1)
        {
            while (thread_num--)
                threads_.emplace_back([this] { this->loop(); });
        }
        ~ThreadPool()
        {
            stop();
            cond_.notify_all();
        }

        template <typename Function, typename... Args>
        auto add_task(Function &&func, Args &&...args)
        {
            using return_type = decltype(func(args...));
            using pack_task_type = std::packaged_task<return_type()>;
            auto task = std::make_shared<pack_task_type>(
                std::bind(std::forward<Function>(func), std::forward<Args>(args)...));
            {
                std::lock_guard<std::mutex> lg{mtx_};
                tasks_.emplace([task] { std::invoke(*task); std::cout << std::this_thread::get_id() << std::endl;std::this_thread::sleep_for(std::chrono::seconds(1)); });
            }
            cond_.notify_one();
            return task->get_future();
        }
        bool add_thread(int num)
        {
            std::unique_lock<std::mutex> ulk(this->mtx_);
            while (num--)
                threads_.emplace_back([this] { this->loop(); });
            return true;
        }
        void start()
        {
            run_flag_.store(true, std::memory_order_release);
        }
        void stop()
        {
            run_flag_.store(false, std::memory_order_release);
        }
        auto is_run()
        {
            return run_flag_.load(std::memory_order_acquire);
        }
        auto task_size()
        {
            return tasks_.size();
        }
        auto thread_size()
        {
            return threads_.size();
        }

    private:
        void loop()
        {
            while (is_run())
            {
                task_type task;
                {
                    std::unique_lock<std::mutex> ulk(this->mtx_);
                    this->cond_.wait(ulk, [this] {
                        return !is_run() || !this->tasks_.empty();
                    });
                    if (!is_run())
                        return;
                    task = std::move(this->tasks_.front());
                    this->tasks_.pop();
                }
                std::invoke(task);
            }
        }

    private:
        using task_type = std::function<void()>;
        std::vector<std::jthread> threads_;
        std::queue<task_type> tasks_;
        std::atomic_flag add_flag_;
        std::atomic_bool run_flag_;
        std::mutex mtx_;
        std::condition_variable cond_;
    };

}


#endif