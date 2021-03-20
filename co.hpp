#ifndef CO_HPP_
#define CO_HPP_

#include <coroutine>

#include <bits/stdc++.h>
#include <sys/epoll.h>
#include <sys/socket.h>

namespace xp
{
    template <typename P>
    concept Promise = requires(P p)
    {
        p.get_return_object();
        p.initial_suspend();
        p.final_suspend();
        p.return_void();
        p.unhandled_exception();
    };

    inline namespace V1
    {
        struct Awaiter
        {
            bool await_ready() const noexcept { return false; }
            void await_suspend(std::coroutine_handle<> handle) noexcept {}
            void await_resume() noexcept {}
        };

        struct AwaiterFunc
        {
            std::function<bool()> ready;
            AwaiterFunc(std::function<bool()> r = [] { return false; })
                : ready{r}
            {
            }
            bool await_ready() const noexcept { return ready(); }
            void await_suspend(std::coroutine_handle<> handle) noexcept {}
            void await_resume() noexcept {}
        };

        template <typename T = void>
        class Task;

        template <>
        class Task<void>
        {
        public:
            struct promise_type;
            using coroutine_handle = std::coroutine_handle<promise_type>;
            Task() noexcept : handle_(nullptr) {}
            Task(coroutine_handle handle) noexcept : handle_(handle) {}
            Task(Task &&task) noexcept : handle_(task.handle_)
            {
                task.handle_ = nullptr;
            }
            Task &operator=(Task &&task) noexcept
            {
                if (handle_) [[liakely]]
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

        protected:
            coroutine_handle handle_;
        };

        template <typename T>
        class Task : public Task<>
        {
        public:
            using value_type = T;
            using coroutine_handle = std::coroutine_handle<promise_type>;
            struct promise_type;

            using Task<>::Task;

            auto value() noexcept
            {
                return handle_.promise().value;
            }
        };

        struct Task<>::promise_type
        {
            using coroutine_handle = std::coroutine_handle<promise_type>;
            auto get_return_object()
            {
                return coroutine_handle::from_promise(*this);
            }
            auto initial_suspend()
            {
                return std::suspend_never();
            }
            // Ignore this error.It can work.
            auto final_suspend()
            {
                return std::suspend_always();
            }
            void return_void() {}
            void unhandled_exception()
            {
                std::terminate();
            }
        };

        template <typename T>
        struct Task<T>::promise_type : public Task<>::promise_type
        {
            auto yield_value(T t)
            {
                value_ = std::move(t);
                return std::suspend_always();
            }
            auto value()
            {
                return value_;
            }
            T value_;
        };
    }

    namespace V2
    {
        struct defualt_promise_type
        {
            using coroutine_handle = std::coroutine_handle<defualt_promise_type>;
            auto get_return_object() noexcept
            {
                return coroutine_handle::from_promise(*this);
            }
            auto initial_suspend() const noexcept
            {
                return std::suspend_never();
            }
            auto final_suspend() const noexcept
            {
                return std::suspend_always();
            }
            void return_void() const noexcept
            {
            }
            void unhandled_exception() const noexcept
            {
                std::terminate();
            }
        };

        struct defualt_awaiter
        {
            static bool await_ready() noexcept { return false; }
            static void await_suspend(std::coroutine_handle<>) noexcept {}
            static void await_resume() noexcept {}
        };

        template <typename T = void>
        struct Task;

        template <>
        struct Task<void>
        {
            using promise_type = defualt_promise_type;
            using coroutine_handle = std::coroutine_handle<promise_type>;
            Task() noexcept : handle_(nullptr) {}
            Task(coroutine_handle handle) noexcept : handle_(handle) {}
            Task(Task &&task) noexcept : handle_(task.handle_)
            {
                task.handle_ = nullptr;
            }
            Task &operator=(Task &&task) noexcept
            {
                if (handle_) [[liakely]]
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
            coroutine_handle handle_;
        };
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

    Task<int> co_read(const int fd)
    {
    }

    Task<int> co_write(const int fd, std::vector<char> buf, const int num)
    {
        int res = 0;

        ::send(fd, buf.data(), num, MSG_DONTWAIT);
    }

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