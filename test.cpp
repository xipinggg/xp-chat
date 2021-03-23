#include <iostream>
#include "logger.hpp"
#include "co.hpp"
#include "event_manager.h"
using namespace std;
using namespace xp;
std::coroutine_handle<> g_handle;

struct Task
{
    struct promise_type;
    using coroutine_handle = std::coroutine_handle<promise_type>;
    Task() noexcept : handle_(nullptr) {}
    Task(coroutine_handle handle) noexcept : handle_(handle) {}
    Task(promise_type &promise) : handle_(std::coroutine_handle<promise_type>::from_promise(promise)) {}
    Task(Task &&task) noexcept : handle_(task.handle_)
    {
        task.handle_ = nullptr;
    }
    Task &operator=(Task &&task) noexcept
    {
        if (handle_) [[likely]]
        {
            handle_.destroy();
        }
        handle_ = task.handle_;
        task.handle_ = nullptr;
        return *this;
    }
    Task &operator=(const Task &) = delete;
    Task(const Task &) = delete;
    ~Task()
    {
        if (handle_) [[likely]]
        {
            handle_.destroy();
        }
    }
    bool done() noexcept
    {
        return handle_.done();
    }
    void resume() noexcept
    {
        if (handle_ && !handle_.done())
        {
            handle_.resume();
        }
    }
    void destory() noexcept
    {
        if (handle_) [[likely]]
        {
            handle_.destroy();
            handle_ = nullptr;
        }
    }
    struct promise_type : public xp::BasicPromise
    {
        using coroutine_handle = std::coroutine_handle<promise_type>;
        std::coroutine_handle<> waiter = std::noop_coroutine();
        auto get_return_object() noexcept
        {
            return coroutine_handle::from_promise(*this);
            //return *this;
        }
        auto final_suspend() noexcept
        {
            struct Awaiter
            {
                std::coroutine_handle<> waiter;
                bool await_ready() noexcept { return false; }
                auto await_suspend(std::coroutine_handle<> handle) noexcept
                {
                    return waiter;
                }
                void await_resume() noexcept {}
            };
            return Awaiter{waiter};
        }
    };
    bool await_ready() noexcept
    {
        return false;
    }
    void await_suspend(std::coroutine_handle<> caller) noexcept
    {
        handle_.promise().waiter = caller;
    }
    void await_resume() noexcept
    {
    }
    coroutine_handle handle_;
};

struct EventAwaiter
{
    bool await_ready() noexcept { return false; }
    void await_suspend(std::coroutine_handle<> handle) noexcept
    {
        g_handle = handle;
    }
    void await_resume() noexcept {}
};

struct my_promise : public xp::BasicPromise
{
    using coroutine_handle = std::coroutine_handle<my_promise>;
    std::coroutine_handle<> waiter = std::noop_coroutine();
    auto get_return_object() noexcept
    {
        return coroutine_handle::from_promise(*this);
    }
    auto final_suspend() noexcept
    {
        return xp::FinalAwaiter<>{waiter};
    }
};

struct MyTask : public xp::BasicTask<my_promise>
{
    usingBasicTask;
    bool await_ready() noexcept
    {
        return false;
    }
    void await_suspend(std::coroutine_handle<> caller) noexcept
    {
        handle.promise().waiter = caller;
    }
    void await_resume() noexcept
    {
    }
};

MyTask callee2()
{
    xp::log("4");
    co_await EventAwaiter{};
    xp::log("6");
    co_return;
}
MyTask callee()
{
    xp::log("3");
    co_await callee2();
    xp::log("7");
    co_return;
}
MyTask waiter()
{
    xp::log("2");
    co_await callee();
    xp::log("8");
    co_return;
}

xp::EventLoop::handle_event_type handle_event = [](epoll_event epevent) {
    xp::log();
    auto handle = std::coroutine_handle<>::from_address(epevent.data.ptr);
    
    handle.resume();
    return -1;
};

xp::EventLoop loop{handle_event};

int main()
{
    /*
    xp::log("1");
    auto f = waiter();
    xp::log("5");
    g_handle();
    xp::log("9");*/
    auto wakeup = xp::co_wakeup(loop.fd());
    auto ptr = wakeup.handle.address();
    loop.set_wakeup_handler(ptr);
    xp::log();
    assert(loop.wakeup());
    loop.start();
    xp::log();
}