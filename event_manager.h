#ifndef EVENT_MANAGER_H_
#define EVENT_MANAGER_H_

#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <sys/eventfd.h>
#include <cassert>
#include <unistd.h>

#include <vector>
#include <iostream>
#include <span>
#include <mutex>
#include <exception>
#include <stdexcept>
#include <functional>

#include "logger.hpp"
namespace xp
{
    epoll_event make_epoll_event(const epoll_data_t data, const uint32_t events = EPOLLIN | EPOLLPRI | EPOLLHUP)
    {
        epoll_event event;
        event.events = events;
        event.data = data;
        /*
            *    void *ptr;
            *    int fd;
            *    uint32_t u32;
            *    uint64_t u64;
            */
        return event;
    }

    class Epoller
    {
    public:
        Epoller() : epollfd_{epoll_create1(EPOLL_CLOEXEC)}
        {
            log(fmt::format("epollfd={}", epollfd_));
            if (epollfd_ < 0)
                throw std::bad_exception{};
        }
        Epoller(const Epoller &) = delete;
        Epoller &operator=(const Epoller &) = delete;
        Epoller(Epoller &&) = delete;
        Epoller &operator=(Epoller &&) = delete;
        ~Epoller()
        {
            log(fmt::format("close epollfd : {}", epollfd_));
            ::close(epollfd_);
        }
        int ctl(const int option, const int fd, epoll_event *event)
        {
            log(fmt::format("option={},fd={}", option, fd));
            std::lock_guard<decltype(mtx_)> lg{mtx_};
            return ::epoll_ctl(epollfd_, option, fd, event);
        }
        int epoll(std::vector<epoll_event> &events, const int timeout = 0)
        {
            log();
            auto *buf = events.data();
            assert(buf);
            const int max_num = events.capacity();
            assert(max_num > 0);
            std::lock_guard<decltype(mtx_)> lg{mtx_};
            return epoll_wait(epollfd_, buf, max_num, timeout);
        }
        std::span<epoll_event> epoll(const int num = 0)
        {
            log();
            constexpr int TIMEOUT = -1;

            int max_num = num;
            if (num == 0)
            {
                max_num = events_.size();
            }
            else if (num > events_.capacity())
            {
                events_.reserve(num);
            }
            auto *buf = events_.data();
            assert(buf);
            std::lock_guard<std::mutex> lg{mtx_};
            const int events_num = epoll_wait(epollfd_, buf, max_num, TIMEOUT);
            return {events_.begin(), events_num};
        }

    private:
        int epollfd_;
        std::vector<epoll_event> events_;
        std::mutex mtx_;
    };

    class Timer
    {
    public:
        friend class TimerManager;
        Timer(const int clockid = CLOCK_REALTIME)
            : fd_{timerfd_create(clockid, TFD_CLOEXEC | TFD_NONBLOCK)}
        {
            log();
            if (fd_ < 0) [[unlikely]]
                throw std::bad_exception{};
        }
        ~Timer()
        {
            log();
            ::close(fd_);
        }
        int set(const int flags, const itimerspec *new_value, itimerspec *old_value = nullptr) noexcept
        {
            return timerfd_settime(fd_, flags, new_value, old_value);
        }
        int set_absolute(const itimerspec *new_value, itimerspec *old_value = nullptr) noexcept
        {
            return timerfd_settime(fd_, TFD_TIMER_ABSTIME, new_value, old_value);
        }
        int set_relative(const itimerspec *new_value, itimerspec *old_value = nullptr) noexcept
        {
            return timerfd_settime(fd_, 0, new_value, old_value);
        }
        int get(itimerspec *old_value) noexcept
        {
            return timerfd_gettime(fd_, old_value);
        }

    private:
        int fd_;
    };
    class TimerManager
    {
    public:
        TimerManager() {}
        ~TimerManager() {}

    private:
    };

    class Event
    {
    public:
        Event() : fd_{eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK)}
        {
        }
        Event(std::function<int()> make_fd) : fd_{make_fd()}
        {
            if (fd_ < 0)
                throw std::bad_exception{};
        }
        ~Event()
        {
            ::close(fd_);
        }
        int fd() const
        {
            return fd_;
        }
        void handle(epoll_event epevent)
        {
        }
        void handle_read()
        {
        }
        void handle_write()
        {
        }
        void handle_close()
        {
        }
        void handle_error()
        {
        }

    private:
        int fd_ = -1;
    };

    class EventLoop
    {
    public:
        using task_type = std::function<void()>;
        using handle_event_type = std::function<int(epoll_event)>;
        enum ctl_option
        {
            add = EPOLL_CTL_ADD,
            del = EPOLL_CTL_DEL,
            mod = EPOLL_CTL_MOD,
        };
        EventLoop()
        {
            log();
        }
        EventLoop(handle_event_type handle_event)
            : fd_{eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK)}, handle_event_{handle_event}
        {
            log(fmt::format("eventfd={}", fd_));
            if (fd_ < 0)
            {
                throw std::bad_exception{};
            }
        }
        EventLoop(const EventLoop &) = delete;
        EventLoop &operator=(const EventLoop &) = delete;
        EventLoop(EventLoop &&) = delete;
        EventLoop &operator=(EventLoop &&) = delete;
        ~EventLoop()
        {
            ::close(fd_);
        }
        void start()
        {
            log();
            std::vector<epoll_event> events(10);
            events.clear();
            while (true)
            {
                sleep(1);
                int num = epoller_.epoll(events);
                log(fmt::format("epoll result : {}", num), "info");
                //std::cout << std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()) <<std::endl;
                for (int i = 0; i < num; ++i)
                {
                    auto epevent = events[i];
                    if (0 > handle_event_(epevent))
                    {
                    }
                }
            }
        }
        int ctl(ctl_option option, int fd, epoll_event *event)
        {
            xp::log();
            return epoller_.ctl(option, fd, event);
        }
        /*template <class Function, class... Args>
        std::future<typename std::result_of<Function(Args...)>::type>
        add_task(Function &&fcn, Args &&...args)
        {
            using return_type = typename std::result_of<Function(Args...)>::type;
            using task = std::packaged_task<return_type()>;

            auto t = std::make_shared<task>(std::bind(std::forward<Function>(fcn), std::forward<Args>(args)...));
            auto ret = t->get_future();
            tasks_.emplace([t] { (*t)(); });
            return ret;
        }*/
        void add_task(std::function<void()> task)
        {
        }
        int set_wakeup_handler(void *ptr)
        {
            xp::log();
            auto data = epoll_data_t{.ptr = ptr};
            auto epevent = make_epoll_event(data);
            return ctl(ctl_option::add, fd_, &epevent);
        }
        bool wakeup()
        {
            xp::log();
            return eventfd_write(fd_, 1) >= 0;
        }
        int fd() const noexcept { return fd_; }

    private:
        int fd_ = -1;
        std::vector<epoll_event> events_;
        std::list<task_type> tasks_;
        std::mutex mtx_;
        xp::Epoller epoller_;
        handle_event_type handle_event_;
    };
}

#endif