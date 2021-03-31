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
extern thread_local epoll_event th_epevent;
extern int sleep_time;
namespace xp
{
    thread_local int to_del_fd = -1;

    struct ReadPromise : public BasicPromise
    {
        using coroutine_handle = std::coroutine_handle<ReadPromise>;
        auto get_return_object() noexcept
        {
            return coroutine_handle::from_promise(*this);
        }
        auto yield_value(int i)
        {
            value = i;
            return std::suspend_always{};
        }
        int fd;
        void *buf;
        size_t num;
        int value;
    };
    struct ReadTask : public BasicTask<ReadPromise>
    {
        usingBasicTask;
        auto resume() noexcept
        {
            if (handle && !handle.done())
            {
                handle.resume();
            }
            return handle.promise().value;
        }
        auto resume(void *p, size_t num) noexcept
        {
            handle.promise().buf = p;
            handle.promise().num = num;
            handle.resume();
            return handle.promise().value;
        }
    };
    ReadTask co_read(int fd, void *&buf, size_t &num)
    {
        xp::log();
        std::suspend_always{};
        while (buf)
        {
            auto result = ::recv(fd, buf, num, MSG_DONTWAIT);
            xp::log();
            if (result == num) [[likely]]
            {
                co_await CallerAwaiter<>{};
            }
            else if (result <= 0)
            {
                xp::log(fmt::format("errno={}", errno));
                if (errno & EINTR || errno & EWOULDBLOCK || errno & EAGAIN)
                {
                    co_yield 1;
                }
                else
                {
                    co_yield -1;
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

    using WriteTask = ReadTask;
    WriteTask co_write(const int fd, void *buf, int num)
    {
        xp::log();
        std::suspend_always{};
        while (buf)
        {
            auto result = ::send(fd, buf, num, MSG_DONTWAIT);
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
/*class Acceptor
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
        BasicTask<> co_accept(std::function<void(int, sockaddr_in)> handler)
        {
            xp::log();
            co_await std::suspend_always{};
            while (true)
            {
                int new_fd = -1;
                sockaddr_in addr;
                {
                    socklen_t len = sizeof(addr);
                    new_fd = ::accept(fd_, (sockaddr *)&addr, &len);
                }
                if (new_fd >= 0)
                {
                    handler(new_fd, addr);
                }
                else
                {
                    co_await std::suspend_always{};
                }
            }
            co_return;
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

*/

    using ConnTask = AutoDestroyTask<FinalSuspendPromise>;
    ConnTask co_connection(int fd, sockaddr_in addr)
    {
        xp::log();
        co_await std::suspend_always{};
        std::string write_buf(64, '\0');
        std::string read_buf(64, '\0');
        char *write_ptr = write_buf.data();
        char *read_ptr = read_buf.data();

        constexpr int defualt_num = 10;
        int write_num = defualt_num;
        int read_num = defualt_num;
        // 验证
        int result = 0;
        bool flag = true;

        while (flag)
        {
            {
                sleep(sleep_time);
                log();
                int events = th_epevent.events;

                if (events & EPOLLIN)
                {
                    log();
                    result = ::recv(fd, read_ptr, read_num, MSG_DONTWAIT);
                    log(fmt::format("recv result={}", result));
                    if (result == 0)
                    {
                        xp::log();
                        break;
                    }
                    else if (result < 0)
                    {
                        log();
                        std::cout << "errno\n";
                        if (errno == EINTR || errno == EAGAIN)
                        {
                            xp::log();
                        }
                        else
                        {
                            xp::log();
                            break;
                        }
                    }
                    else if (result == read_num)
                    {
                        xp::log();
                        log(fmt::format("{} says : {}", fd, std::string_view(read_buf.data(), defualt_num)));
                        read_num = defualt_num;
                        read_ptr = &read_buf[0];
                        copy(read_buf.begin(), read_buf.end(), write_buf.begin());
                        events |= EPOLLOUT;
                        log(fmt::format("events={}", events));
                    }
                    else
                    {
                        xp::log();
                        read_num -= result;
                        read_ptr += result;
                    }
                }

                if (events & EPOLLOUT)
                {
                    log();
                    result = ::send(fd, write_ptr, write_num, MSG_DONTWAIT);
                    log(fmt::format("send result={}", result));
                    if (result == 0)
                    {
                        xp::log();
                        break;
                    }
                    else if (result < 0)
                    {
                        log();
                        std::cout << "errno\n";
                        if (errno == EINTR || errno == EAGAIN)
                        {
                            xp::log();
                        }
                        else
                        {
                            xp::log();
                            break;
                        }
                    }
                    else if (result == write_num)
                    {
                        xp::log();
                        write_num = defualt_num;
                        write_ptr = write_buf.data();
                    }
                    else
                    {
                        xp::log();
                        write_num -= result;
                        write_ptr += result;
                    }
                }

                if (events & EPOLLERR)
                {
                    xp::log(fmt::format("events={}", events));
                    break;
                }

                if (events & EPOLLRDHUP)
                {
                    xp::log(fmt::format("events={}", events));
                    break;
                }
            }
            log();
            co_await std::suspend_always{};
        }
        to_del_fd = fd;
        xp::log("conn co_return");
        co_return;
    }

    struct User
    {
        int id;
        char name[6];
    };

    struct Connection
    {
        int fd;
        sockaddr_in addr;
        ConnTask task;
    };
}

#endif // !CO_NET_H