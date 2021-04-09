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

#include "logger.h"

extern int sleep_time;
namespace xp
{
    enum
    {
        default_epoll_events = EPOLLIN | EPOLLPRI | EPOLLHUP
    };
    epoll_event make_epoll_event(const epoll_data_t data,
                                 const uint32_t events = default_epoll_events)
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

    //movable
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
        Epoller(Epoller &&e) noexcept
            : epollfd_{e.epollfd_}, events_{std::move(e.events_)}, mtx_{}
        {
            e.epollfd_ = -1;
        }
        Epoller &operator=(Epoller &&e) noexcept
        {
            epollfd_ = e.epollfd_;
            events_ = std::move(e.events_);

            e.epollfd_ = -1;
            return *this;
        }
        ~Epoller()
        {
            log(fmt::format("close epollfd : {}", epollfd_));
            if (epollfd_ >= 0)
                ::close(epollfd_);
        }
        int ctl(const int option, const int fd, epoll_event *event)
        {
            log(fmt::format("option={},fd={}", option, fd));
            std::lock_guard lg{mtx_};
            return ::epoll_ctl(epollfd_, option, fd, event);
        }
        int epoll(std::vector<epoll_event> &events, const int timeout = 0)
        {
            log();
            auto *buf = events.data();
            assert(buf);
            const int max_num = events.capacity();
            assert(max_num > 0);
            std::lock_guard lg{mtx_};
            return epoll_wait(epollfd_, buf, max_num, timeout);
        }
        /*std::span<epoll_event> epoll(const int num = 0)
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
            int events_num = epoll_wait(epollfd_, buf, max_num, TIMEOUT);
            events_num = events_num < 0 ? 0 : events_num;
            return {events_.begin(), events_num};
        }*/

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

    //movable
    class EventLoop
    {
    public:
        using task_type = std::function<void()>;
        using event_handler_type = std::function<void(epoll_event)>;
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
        EventLoop(event_handler_type event_handler)
            : fd_{eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK)}, event_handler_{event_handler}
        {
            log(fmt::format("eventfd={}", fd_));
            if (fd_ < 0)
            {
                throw std::bad_exception{};
            }
            /*auto ptr = get_wakeup_handler(fd_);
            auto data = epoll_data_t{.ptr = ptr};
            auto epevent = make_epoll_event(data);
            ctl(ctl_option::add, fd_, &epevent);*/
        }
        EventLoop(const EventLoop &) = delete;
        EventLoop &operator=(const EventLoop &) = delete;
        EventLoop(EventLoop &&e) noexcept
            : fd_{e.fd_}, events_{std::move(e.events_)}, tasks_{std::move(e.tasks_)}, mtx_{},
              epoller_{std::move(e.epoller_)}, event_handler_{e.event_handler_}
        {
            e.fd_ = -1;
        }
        EventLoop &operator=(EventLoop &&e) noexcept
        {
            fd_ = e.fd_;
            events_ = std::move(e.events_);
            tasks_ = std::move(tasks_);
            epoller_ = std::move(e.epoller_);
            event_handler_ = e.event_handler_;

            e.fd_ = -1;
            return *this;
        }
        ~EventLoop()
        {
            if (fd_ >= 0)
                ::close(fd_);
        }
        void start(int timeout)
        {
            log();
            std::vector<epoll_event> events(64);
            events.clear();
            while (true)
            {
                sleep(sleep_time);
                int num = epoller_.epoll(events, timeout);
                log(fmt::format("epoll result : {}", num), "info");
                for (int i = 0; i < num; ++i)
                {
                    auto epevent = events[i];
                    event_handler_(epevent);
                }
                do_tasks();
            }
        }

        int ctl(ctl_option option, int fd, epoll_event *event)
        {
            xp::log(fmt::format("option={}", option));
            return epoller_.ctl(option, fd, event);
        }
        void commit_ctl(ctl_option option, int fd, epoll_event *event)
        {
            log();
            
        }
        void add_task(std::function<void()> task)
        {
            tasks_.add(std::move(task));
        }
        bool wakeup() noexcept
        {
            xp::log();
            return eventfd_write(fd_, 1) >= 0;
        }
        void on_wakeup()
        {
            xp::log();
            eventfd_t count{0};
            eventfd_read(fd_, &count);
        }
        int fd() const noexcept { return fd_; }

    private:
        void do_tasks()
        {
            if (tasks_.empty())
                return;
            auto tasks = tasks_.get();
            for (auto &task : tasks)
            {
                task();
            }
        }

    private:
        int fd_ = -1;
        std::vector<epoll_event> events_;
        xp::SwapBuffer<task_type> tasks_;
        std::mutex mtx_;
        xp::Epoller epoller_;
    public:
        event_handler_type event_handler_;
    };

    class EventLoopManager
    {
    public:
        EventLoopManager(uint num, EventLoop::event_handler_type handler)
            : loops_num_{num}
        {
            while (num--)
            {
                loops_.emplace_back(handler);
            }
        }
        ~EventLoopManager()
        {
        }
        EventLoop *select(const uint fd, uint &idx) noexcept
        {
            idx = fd % loops_num_;
            return &loops_[idx];
        }
        EventLoop *select(const int fd) noexcept
        {
            log();
            const int idx = fd % loops_num_;
            return &loops_[idx];
        }
        EventLoop &operator[](const int idx)
        {
            return loops_.at(idx);
        }

    private:
        int loops_num_;
        std::vector<EventLoop> loops_;
        std::mutex mtx_;
    };

}

#endif