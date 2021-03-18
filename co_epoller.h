#ifndef CO_EPOLLER_H
#define CO_EPOLLER_H

#include <sys/epoll.h>
#include <cassert>
#include <unistd.h>
#include <vector>
#include <iostream>
#include <span>
#include <mutex>
#define trace() std::cout << __FUNCTION__ << " : " \
                          << __FILE__ << " : " << __LINE__ << std::endl

extern thread_local std::vector<struct epoll_event> revents;

//template<typename Event_data_t>
class Epoller
{
public:
    using Event_data_ptr = std::pair<const int, Task> *;

    Epoller() : epollfd_(epoll_create1(EPOLL_CLOEXEC))
    {
        assert(epollfd_ >= 0);
    }
    Epoller(const Epoller &) = delete;
    Epoller &operator=(const Epoller &) = delete;
    Epoller(Epoller &&) = delete;
    Epoller &operator=(Epoller &&) = delete;
    ~Epoller()
    {
        ::close(epollfd_);
    }
    /*int epoll()
    {
        constexpr int TIMEOUT = -1;
        auto *buf = revents.data();
        revents.clear();
        const int max_size = revents.capacity();
        int events_num = epoll_wait(epollfd_, buf, max_size, TIMEOUT);

        return events_num;
    }*/
    int epoll(std::vector<epoll_event> &events,const int num = 0)
    {
        
        constexpr int TIMEOUT = -1;
        auto *buf = events.data();
        const int max_num = num == 0 ? events.size() : num;
        std::lock_guard<mutex> lg{mtx_};
        const int events_num = epoll_wait(epollfd_, buf, max_num, TIMEOUT);
        return events_num;
    }
    std::span<epoll_event> epoll(const int num = 0)
    {
        constexpr int TIMEOUT = -1;
        auto *buf = events_.data();
        int max_num = num;
        if (num == 0)
        {
            max_num = events_.size();
        }
        else if (num > events_.capacity())
        {
            events_.reserve(num);
        } 
        std::lock_guard<mutex> lg{mtx_};
        const int events_num = epoll_wait(epollfd_, buf, max_num, TIMEOUT);
        return {events_.begin(), events_num};
    }
    int ctl(int option, int fd, epoll_event *event) 
    {
        std::lock_guard<mutex> lg{mtx_};
        return ::epoll_ctl(epollfd_, option, fd, event);
    }
    int fd() const noexcept { return epollfd_; }
    static epoll_event make_event(void *ptr, uint32_t events = EPOLLIN | EPOLLPRI | EPOLLHUP)
    {
        epoll_event event;
        event.events = events;
        event.data.ptr = ptr;
        return event;
    }

private:
    int epollfd_;
    std::vector<epoll_event> events_;
    std::mutex mtx_;
};

#endif // !CO_EPOLLER_H