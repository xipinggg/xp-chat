#ifndef CO_HPP_
#define CO_HPP_

#include <coroutine>

#include <bits/stdc++.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/timerfd.h>
#include <sys/eventfd.h>
#include "logger.h"

namespace xp
{
    extern thread_local int to_del_fd;
    template <typename P>
    concept PromiseType = requires(P p)
    {
        p.get_return_object();
        p.initial_suspend();
        p.final_suspend();
        p.return_void();
        p.unhandled_exception();
    };

    template <typename A>
    concept AwaiterType = requires(A a)
    {
        a.await_ready();
        a.await_suspend();
        a.await_resume();
    };

    template <typename promise>
    concept FinalSuspendPromiseType = requires(promise p)
    {
        p.final_suspend().await_ready() == false;
    };

    template <typename promise_type = void>
    struct CallerAwaiter
    {
        std::coroutine_handle<promise_type> waiter;
        constexpr bool await_ready() noexcept { return false; }
        auto await_suspend(std::coroutine_handle<promise_type> handle) noexcept
        {
            return waiter;
        }
        constexpr void await_resume() noexcept {}
    };

    //not final suspend
    struct BasicPromise
    {
        using coroutine_handle = std::coroutine_handle<BasicPromise>;
        auto get_return_object() noexcept
        {
            return coroutine_handle::from_promise(*this);
        }
        constexpr auto initial_suspend() const noexcept
        {
            return std::suspend_never{};
        }
        constexpr auto final_suspend() const noexcept
        {
            return std::suspend_never{};
        }
        constexpr void return_void() const noexcept
        {
        }
        void unhandled_exception() const noexcept
        {
            log("", "error");
            std::terminate();
        }
    };

    struct FinalSuspendPromise : public BasicPromise
    {
        using coroutine_handle = std::coroutine_handle<FinalSuspendPromise>;
        auto get_return_object() noexcept
        {
            return coroutine_handle::from_promise(*this);
        }
        constexpr auto final_suspend() const noexcept
        {
            return std::suspend_always{};
        }
    };

    struct AwaitedPromise : public BasicPromise
    {
        using coroutine_handle = std::coroutine_handle<AwaitedPromise>;
        std::coroutine_handle<> waiter = std::noop_coroutine();
        auto get_return_object() noexcept
        {
            return coroutine_handle::from_promise(*this);
        }
        constexpr auto final_suspend() noexcept
        {
            return CallerAwaiter<>{waiter};
        }
    };

    //not auto destroy handle
    template <PromiseType promise_t = BasicPromise>
    struct BasicTask
    {
        using promise_type = promise_t;
        using coroutine_handle = std::coroutine_handle<promise_type>;
        BasicTask() noexcept : handle(nullptr) {}

        BasicTask(BasicTask &) = default;
        BasicTask &operator=(BasicTask &) = default;
        BasicTask(BasicTask &&) = default;
        BasicTask &operator=(BasicTask &&) = default;

        BasicTask(coroutine_handle hd) noexcept : handle(hd) {}
        BasicTask(promise_type &promise)
            : handle(std::coroutine_handle<promise_type>::from_promise(promise)) {}
        BasicTask(void *p)
            : handle(std::coroutine_handle<promise_type>::from_address(p)) {}
        //not destroy handle
        ~BasicTask() {}

        bool done() noexcept
        {
            return handle.done();
        }
        void resume() noexcept
        {
            if (handle && !handle.done())
            {
                handle.resume();
            }
        }

        bool await_ready() noexcept
        {
            return handle.done();
        }
        void await_suspend(std::coroutine_handle<> caller) noexcept
        {
            log();
            if constexpr (requires { handle.promise().waiter; })
            {
                log();
                handle.promise().waiter = caller;
            }
        }
        void await_resume() noexcept
        {
        }
        coroutine_handle handle;

#define usingBasicTask          \
    using BasicTask::BasicTask; \
    using BasicTask::operator=
    };

    template <FinalSuspendPromiseType promise_t = AwaitedPromise>
    struct AutoDestroyTask
    {
        using promise_type = promise_t;
        using coroutine_handle = std::coroutine_handle<promise_type>;
        AutoDestroyTask() noexcept : handle(nullptr) {}

        AutoDestroyTask(const AutoDestroyTask &) = delete;
        AutoDestroyTask &operator=(const AutoDestroyTask &) = delete;
        AutoDestroyTask(AutoDestroyTask &&task) noexcept : handle{task.handle}
        {
            task.handle = nullptr;
        }
        AutoDestroyTask &operator=(AutoDestroyTask &&task) noexcept
        {
            handle = task.handle;
            task.handle = nullptr;
        }
        AutoDestroyTask(coroutine_handle hd) noexcept
            : handle(hd) {}
        AutoDestroyTask(promise_type &promise)
            : handle{std::coroutine_handle<promise_type>::from_promise(promise)} {}
        AutoDestroyTask(void *p)
            : handle{std::coroutine_handle<promise_type>::from_address(p)} {}

        ~AutoDestroyTask()
        {
            if (handle)
            {
                log();
                handle.destroy();
            }
        }
        bool done() noexcept
        {
            return handle.done();
        }
        void resume() noexcept
        {
            if (handle && !handle.done())
            {
                handle.resume();
            }
        }
        bool await_ready() noexcept
        {
            return handle && handle.done();
        }
        void await_suspend(std::coroutine_handle<> caller) noexcept
        {
            if constexpr (requires { handle.promise().waiter; })
            {
                handle.promise().waiter = caller;
            }
        }
        void await_resume() noexcept
        {
        }

        coroutine_handle handle;
    };

    BasicTask<> co_wakeup(const int fd)
    {
        xp::log();
        co_await std::suspend_always{};
        while (true)
        {
            {
                xp::log(fmt::format("wakeup fd={}", fd));
                eventfd_t count{0};
                eventfd_read(fd, &count);
                xp::log(fmt::format("wakeup count={}", (uint)count));
            }
            co_await std::suspend_always{};
        }
    }

    BasicTask<> co_loop_func(std::function<bool()> func)
    {
        xp::log();
        co_await std::suspend_always{};
        while (true)
        {
            if (!func())
                co_await std::suspend_always{};
        }
        co_return;
    }

    template <PromiseType promise_t = BasicPromise>
    struct AutoTask
    {
        using promise_type = promise_t;
        using coroutine_handle = std::coroutine_handle<promise_type>;
        coroutine_handle handle;
        AutoTask() noexcept : handle(nullptr) {}

        AutoTask(const AutoTask &) = delete;
        AutoTask &operator=(const AutoTask &) = delete;
        AutoTask(AutoTask &&task) noexcept
            : handle{task.handle}
        {
            task.handle = nullptr;
        }
        AutoTask &operator=(AutoTask &&task) noexcept
        {
            handle = task.handle;
            task.handle = nullptr;
        }

        AutoTask(coroutine_handle hd) noexcept
            : handle(hd) {}
        AutoTask(promise_type &promise)
            : handle{std::coroutine_handle<promise_type>::from_promise(promise)} {}
        AutoTask(void *p)
            : handle{std::coroutine_handle<promise_type>::from_address(p)} {}

        ~AutoTask()
        {
            if constexpr (requires { FinalSuspendPromiseType<promise_type>; })
            {
                if (handle)
                {
                    handle.destroy();
                }
            }
        }
        bool done() noexcept
        {
            return handle && handle.done();
        }
        void resume() noexcept
        {
            if (handle && !handle.done())
            {
                handle.resume();
            }
        }
        bool await_ready() noexcept
        {
            return handle && handle.done();
        }
        void await_suspend(std::coroutine_handle<> caller) noexcept
        {
            if constexpr (requires { handle.promise().waiter; })
            {
                handle.promise().waiter = caller;
            }
        }
        void await_resume() noexcept
        {
        }
    };

    class CoManager
    {
    public:
        CoManager()
        {
        }
        ~CoManager()
        {
        }

    private:
        //std::unordered_map<coroutine_handle<>,> coros_
        std::mutex mtx_;
        xp::SpinLock spin_lock_;
    };

    template <typename Value>
    class Channel
    {
    public:
        using value_type = Value;
        auto operator<<(Value data)
        {
            if (data_.empty())
                return std::suspend_always{};

            return std::suspend_never{};
        }
        auto operator<<(Value &&data)
        {
        }
        auto operator>>(Value &data)
        {
        }

    private:
        std::list<Value> data_;
        std::list<int> waiters_;
    };

}
#endif // !CO_HPP_

/*
template <typename Promise = void>
struct coroutine_handle;

template <typename Promise>
struct coroutine_handle::coroutine_handle<>
{
    using coroutine_handle<>::coroutine_handle;
    static coroutine_handle from_promise(Promise &);
    coroutine_handle &operator=(nullptr_t) noexcept;
    constexpr static coroutine_handle from_address(void *addr);
    Promise &promise() const;
};

template <>
struct coroutine_handle<void>
{
    constexpr coroutine_handle() noexcept;
    constexpr coroutine_handle(nullptr_t) noexcept;
    coroutine_handle &operator=(nullptr_t) noexcept;
    constexpr void *address() const noexcept;
    constexpr static coroutine_handle from_address(void *addr);
    constexpr explicit operator bool() const noexcept;
    bool done() const;
    void operator()();
    void resume();
    void destroy();

private:
    void *ptr; // exposition only
};

*/