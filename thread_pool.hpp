#ifndef THREAD_POOL_HPP
#define THREAD_POOL_HPP

#include <bits/stdc++.h>
#include <vector>
#include <queue>
#include <atomic>
#include <future>
#include <condition_variable>
#include <thread>
#include <functional>
#include <stdexcept>
//#include "tow_buffer.hpp"

class ThreadsGuard
{
public:
    ThreadsGuard(std::vector<std::thread> &v)
        : threads_(v)
    {
    }

    ~ThreadsGuard()
    {
        for (size_t i = 0; i != threads_.size(); ++i)
        {
            if (threads_[i].joinable())
            {
                threads_[i].join();
            }
        }
    }

private:
    ThreadsGuard(ThreadsGuard &&tg) = delete;
    ThreadsGuard &operator=(ThreadsGuard &&tg) = delete;

    ThreadsGuard(const ThreadsGuard &) = delete;
    ThreadsGuard &operator=(const ThreadsGuard &) = delete;

private:
    std::vector<std::thread> &threads_;
};

class ThreadPool
{
public:
    using task_type = std::function<void()>;

public:
    explicit ThreadPool(int n = 0);

    ~ThreadPool()
    {
        stop();
        cond_.notify_all();
    }

    void stop()
    {
        stop_.store(true, std::memory_order_release);
    }

    template <class Function, class... Args>
    std::future<typename std::result_of<Function(Args...)>::type>
    add(Function &&, Args &&...);

private:
    ThreadPool(ThreadPool &&) = delete;
    ThreadPool &operator=(ThreadPool &&) = delete;
    ThreadPool(const ThreadPool &) = delete;
    ThreadPool &operator=(const ThreadPool &) = delete;

private:
    std::atomic<bool> stop_;
    std::mutex mtx_;
    std::condition_variable cond_;

    std::queue<task_type> tasks_;
    std::vector<std::thread> threads_;
    ThreadsGuard tg_;
};

inline ThreadPool::ThreadPool(int n)
    : stop_(false), tg_(threads_)
{
    int nthreads = n;
    if (nthreads <= 0)
    {
        nthreads = std::thread::hardware_concurrency();
        nthreads = (nthreads == 0 ? 2 : nthreads);
    }

    for (int i = 0; i != nthreads; ++i)
    {
        threads_.push_back(std::thread(
            [this] {
                while (!stop_.load(std::memory_order_acquire))
                {
                    task_type task;
                    {
                        std::unique_lock<std::mutex> ulk(this->mtx_);
                        this->cond_.wait(ulk,
                                         [this] {
                                             return stop_.load(std::memory_order_acquire) ||
                                                    !this->tasks_.empty();
                                         });
                        if (stop_.load(std::memory_order_acquire))
                            return;
                        task = std::move(this->tasks_.front());
                        this->tasks_.pop();
                    }
                    task();
                }
            }));
    }
}

template <class Function, class... Args>
std::future<typename std::result_of<Function(Args...)>::type>
ThreadPool::add(Function &&fcn, Args &&...args)
{
    typedef typename std::result_of<Function(Args...)>::type return_type;
    typedef std::packaged_task<return_type()> task;

    auto t = std::make_shared<task>(std::bind(std::forward<Function>(fcn), std::forward<Args>(args)...));
    auto ret = t->get_future();
    {
        std::lock_guard<std::mutex> lg(mtx_);
        if (stop_.load(std::memory_order_acquire))
            throw std::runtime_error("thread pool has stopped");
        tasks_.emplace([t] { (*t)(); });
    }
    cond_.notify_one();
    return ret;
}

class ThreadPool2
{
public:
    ThreadPool2(size_t thread_num = 1)
    {
        while (thread_num--)
            threads_.emplace_back([this] { this->loop(); });
    }
    ~ThreadPool2()
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
#endif