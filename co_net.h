#ifndef CO_NET_H
#define CO_NET_H

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
#include "logger.h"
#include "co.hpp"
namespace xp
{
    BasicTask<AwaitedPromise> co_read(const int fd, void *buf, int num, int &result)
    {
        xp::log();
        std::suspend_always{};
        while (buf)
        {
            result = ::recv(fd, buf, num, MSG_DONTWAIT);
            xp::log();
            if (result == num) [[likely]]
            {
                co_await CallerAwaiter<>{};
            }
            else if (result <= 0)
            {
                xp::log(fmt::format("errno={}", errno));
                if (errno == EINTR || errno == EWOULDBLOCK || errno == EAGAIN)
                {
                    co_await std::suspend_always{};
                }
                else
                {
                    co_await CallerAwaiter<>{};
                }
            }
            else if (result < num)
            {
                num -= result;
                co_await std::suspend_always{};
            }
        }
        xp::log();
        co_return;
    }
    BasicTask<AwaitedPromise> co_write(const int fd, void *buf, int num, int &result)
    {
        xp::log();
        std::suspend_always{};
        while (buf)
        {
            result = ::send(fd, buf, num, MSG_DONTWAIT);
            xp::log();
            if (result == num) [[likely]]
            {
                co_await CallerAwaiter<>{};
            }
            else if (result <= 0)
            {
                xp::log(fmt::format("errno={}", errno));
                if (errno == EINTR || errno == EWOULDBLOCK || errno == EAGAIN)
                {
                    co_await std::suspend_always{};
                }
                else
                {
                    co_await CallerAwaiter<>{};
                }
            }
            else if (result < num)
            {
                num -= result;
                buf = static_cast<char *>(buf) + num;
                co_await std::suspend_always{};
            }
        }
        xp::log();
        co_return;
    }
    class Acceptor
    {
    public:
        Acceptor(const int p = 8888)
            : port_(p),
              fd_(::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP))
        {
            log(fmt::format("fd={}", fd_));
            if (fd_ < 0)
            {
                log(fmt::format("fd={}", fd_), "error");
                throw std::runtime_error("Acceptor fd < 0\n");
            }
            set_fd_nonblock(fd_);
            bind();
        }
        Acceptor(const Acceptor &) = delete;
        Acceptor &operator=(const Acceptor &) = delete;
        Acceptor(Acceptor &&) = delete;
        Acceptor &operator=(Acceptor &&) = delete;
        ~Acceptor() { ::close(fd_); }
        int bind() noexcept
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
        BasicTask<AwaitedPromise> co_accept()
        {
            xp::log();
            co_await std::suspend_always{};
            while (true)
            {
                int new_fd = -1;
                {
                    sockaddr_in addr;
                    socklen_t len = sizeof(addr);
                    new_fd = ::accept(fd_, (sockaddr *)&addr, &len);
                }
                xp::log(fmt::format("accept fd={}", new_fd));
                if (new_fd >= 0)
                {
                    co_await CallerAwaiter<>{};
                }
                else
                {
                    co_await std::suspend_always{};
                }
            }
        }
        std::tuple<int, sockaddr_in> accept() noexcept
        {
            sockaddr_in addr;
            socklen_t len = sizeof(addr);
            const int new_fd = ::accept(fd_, (sockaddr *)&addr, &len);
            return {new_fd, addr};
        }
        static int set_fd_nonblock(int fd) noexcept
        {
            const int flags = fcntl(fd, F_GETFL, 0);
            if ((flags == -1) || (flags & O_NONBLOCK))
            {
                return flags;
            }
            else
            {
                return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
            }
        }
        int port() const noexcept { return port_; }
        int fd() const noexcept { return fd_; }
        sockaddr_in addr() const noexcept { return addr_; }

    private:
        int port_;
        int fd_;
        sockaddr_in addr_;
    };

    NotDestroyTask co_accept(const int fd)
    {
        xp::log();
        co_await std::suspend_always{};
        while (true)
        {
            int new_fd = -1;
            {
                sockaddr_in addr;
                socklen_t len = sizeof(addr);
                new_fd = ::accept(fd, (sockaddr *)&addr, &len);
            }
            xp::log(fmt::format("accept fd={}", new_fd));
            if (new_fd >= 0)
            {
                co_await CallerAwaiter<>{};
            }
            else
            {
                co_await std::suspend_always{};
            }
        }
    }
}
#endif // !CO_NET_H