#ifndef CO_ACCEPTOR_H
#define CO_ACCEPTOR_H

#include <fcntl.h>
#include <unistd.h>
#include <netinet/in.h>
#include <cstring>
#include <cassert>
#include <tuple>
#include <sys/poll.h>
#include <sys/epoll.h>
#include <map>
#include <iostream>

#include "co.hpp"


#define trace() std::cout << __FUNCTION__ << " : " << __FILE__ << " : " << __LINE__ << std::endl

extern int sleep_time;

extern int listen_fd;

//extern std::map<int, Task> coros;

extern thread_local uint32_t cur_co_revents;
extern Epoller epoller;

class Acceptor
{
public:
    Acceptor(const int p = 8888)
        : port_(p),
          fd_(::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP))
    {
        assert(fd_ >= 0);
        set_fd_nonblock(fd_);
    }
    Acceptor(const Acceptor &) = delete;
    Acceptor &operator=(const Acceptor &) = delete;
    Acceptor(Acceptor &&) = delete;
    Acceptor &operator=(Acceptor &&) = delete;
    ~Acceptor() { ::close(fd_); }
    int bind()
    {
        ::bzero(&addr_, sizeof(struct sockaddr_in));
        addr_.sin_family = AF_INET;
        addr_.sin_port = htons(port_);
        addr_.sin_addr.s_addr = htonl(INADDR_ANY);
        const int res = ::bind(fd_, (struct sockaddr *)&addr_, sizeof(addr_));
        assert(res >= 0);
        return res;
    }
    int listen(int backlog = 21)
    {
        return ::listen(fd_, backlog);
    }
    std::tuple<int, sockaddr_in> accept()
    {
        sockaddr_in addr;
        socklen_t len = sizeof(addr);
        const int fd = ::accept(fd_, (sockaddr *)&addr, &len);
        return {fd, addr};
    }
    static int set_fd_nonblock(int fd)
    {
        const int flags = fcntl(fd, F_GETFL, 0);
        if (flags == -1 or flags & O_NONBLOCK)
            return flags;
        else
            return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }
    int port() const { return port_; }
    int fd() const { return fd_; }
    sockaddr_in addr() const { return addr_; }

private:
    int port_;
    int fd_;
    sockaddr_in addr_;
};
/*
Task co_listen_accept(int *fd, std::shared_ptr<uint32_t> revent)
{
    trace();

    Acceptor acceptor;
    acceptor.bind();
    acceptor.listen();
    *fd = acceptor.fd();

    Singleton<TasksManager>::create();
    auto &tasks_manager = *Singleton<TasksManager>::get();
    
    co_await std::suspend_always{};
    auto &lktasks = tasks_manager[*fd];
    while (true)
    {
        sleep(sleep_time);
        auto &tasks = lktasks.tasks;
        auto &shmtx = lktasks.shmtx;
        auto hint = tasks.end();
        while (true)
        {
            auto [fd, addr] = acceptor.accept();
            if (fd >= 0)
            {
                Acceptor::set_fd_nonblock(fd);

                epoll_event event;
                event.events = EPOLLIN | EPOLLPRI | EPOLLHUP;
                const uint32_t events = event.events;
                auto co = co_conn({fd, addr, events}, revent);
                shmtx.lock();
                auto iter = tasks.insert_or_assign(hint, fd, std::move(co));
                event.data.ptr = &(*iter);
                shmtx.unlock();
                epoller.ctl(EPOLL_CTL_ADD, fd, &event);
                hint = ++iter;
            }
            else if (errno == EAGAIN)
            {
                errno = 0;
                break;
            }
        }
        co_await std::suspend_always{};
    }
    co_return;
}
*/
#endif