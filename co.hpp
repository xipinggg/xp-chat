#ifndef CO_HPP_
#define CO_HPP_

#include <coroutine>

#include <bits/stdc++.h>
#include <sys/epoll.h>
#include <sys/socket.h>

namespace xp
{

    struct Awaiter
    {
        bool await_ready() const noexcept { return false; }
        void await_suspend(std::coroutine_handle<> handle) noexcept {}
        void await_resume() noexcept {}
    };

    struct FuncAwaiter
    {
        std::function<bool()> ready;
        FuncAwaiter(std::function<bool()> r = [] { return false; })
            : ready{r}
        {
        }
        bool await_ready() const noexcept { return ready(); }
        void await_suspend(std::coroutine_handle<> handle) noexcept {}
        void await_resume() noexcept {}
    };

    template <typename T = std::any>
    class Task
    {
    public:
        using value_type = T;
        struct promise_type;
        using coroutine_handle = std::coroutine_handle<promise_type>;

        Task(coroutine_handle handle = nullptr) : handle_(handle) {}
        Task(Task &&task) : handle_(task.handle_)
        {
            task.handle_ = nullptr;
        }
        Task &operator=(Task &&task)
        {
            if (handle_ != nullptr)
                handle_.destroy();
            handle_ = task.handle_;
            task.handle_ = nullptr;
            return *this;
        }
        Task &operator=(const Task &) = delete;
        Task(const Task &) = delete;
        ~Task()
        {
            if (handle_ != nullptr)
                handle_.destroy();
        }
        auto value()
        {
            return handle_.promise().value;
        }
        bool done()
        {
            return handle_.done();
        }
        void resume()
        {
            if (!handle_.done() && handle_ != nullptr)
                handle_.resume();
        }
        void operator()()
        {
            resume();
        }
        void resume(epoll_event)
        {
            if (!handle_.done() && handle_ != nullptr)
                handle_.resume();
        }
        void operator()(epoll_event)
        {
            resume();
        }
        void destory()
        {
            handle_.destroy();
            handle_ = nullptr;
        }

    private:
        coroutine_handle handle_;
    };

    template <typename T>
    struct Task<T>::promise_type
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
        auto yield_value(T t)
        {
            //value.emplace(std::move(t));
            value = std::move(t);
            return std::suspend_always();
        }
        void return_void() {}
        void unhandled_exception()
        {
            trace();
            std::terminate();
        }
        T value;
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