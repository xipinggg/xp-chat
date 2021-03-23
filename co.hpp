#ifndef CO_HPP_
#define CO_HPP_

#include <coroutine>

#include <bits/stdc++.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/timerfd.h>
#include <sys/eventfd.h>

namespace xp
{
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

    inline namespace 
    {
        template <typename promise_type = void>
        struct FinalAwaiter
        {
            std::coroutine_handle<promise_type> waiter;
            bool await_ready() noexcept { return false; }
            auto await_suspend(std::coroutine_handle<promise_type> handle) noexcept
            {
                return waiter;
            }
            void await_resume() noexcept {}
        };

        struct BasicPromise
        {
            using coroutine_handle = std::coroutine_handle<BasicPromise>;
            auto get_return_object() noexcept
            {
                return coroutine_handle::from_promise(*this);
            }
            auto initial_suspend() const noexcept
            {
                return std::suspend_never{};
            }
            auto final_suspend() const noexcept
            {
                return std::suspend_never{};
            }
            void return_void() const noexcept
            {
            }
            void unhandled_exception() const noexcept
            {
                std::terminate();
            }
        };

        template <PromiseType promise_t = BasicPromise>
        struct BasicTask
        {
            using promise_type = promise_t;
            using coroutine_handle = std::coroutine_handle<promise_type>;
            BasicTask() noexcept : handle(nullptr) {}
            BasicTask(coroutine_handle hd) noexcept : handle(hd) {}
            BasicTask(promise_type &promise) : handle(std::coroutine_handle<promise_type>::from_promise(promise)) {}
            BasicTask(BasicTask &&task) noexcept : handle(task.handle)
            {
                task.handle = nullptr;
            }
            BasicTask &operator=(BasicTask &&task) noexcept
            {
                if (handle) [[likely]]
                {
                    handle.destroy();
                }
                handle = task.handle;
                task.handle = nullptr;
                return *this;
            }
            BasicTask &operator=(const BasicTask &) = delete;
            BasicTask(const BasicTask &) = delete;
            virtual ~BasicTask()
            {
                if (handle)
                {
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
            coroutine_handle handle;

#define usingBasicTask          \
    using BasicTask::BasicTask; \
    using BasicTask::operator=
        };

        struct WakeupTask : public BasicTask<BasicPromise>
        {
            usingBasicTask;
            ~WakeupTask() {}
        };
        WakeupTask co_wakeup(int fd)
        {
            xp::log();
            co_await std::suspend_always{};
            while (true)
            {
                xp::log(fmt::format("wakeup fd={}",fd));
                {
                    if (eventfd_t count{0}; 0 < eventfd_read(fd, &count))
                    {
                        co_return;
                    }
                }
                co_await std::suspend_always{};
            }
        }

        struct ReadTask : public BasicTask<BasicPromise>
        {
            usingBasicTask;
        };
        ReadTask co_read(const int fd)
        {
        }
        struct WriteTask : public BasicTask<BasicPromise>
        {
        };
        WriteTask co_write(const int fd, std::vector<char> buf, const int num)
        {
            int res = 0;
            ::send(fd, buf.data(), num, MSG_DONTWAIT);
        }
    }

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

}
#endif // !CO_HPP_