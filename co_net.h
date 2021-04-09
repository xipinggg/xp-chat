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
#include "event_manager.h"

extern thread_local epoll_event th_epevent;
extern int sleep_time;

extern thread_local int to_del_fd;
extern xp::EventLoop &accept_loop;

namespace xp 
{
    struct EventAwaiter
    {
        xp::EventLoop *loop;
        int fd;
        bool set;

        constexpr bool await_ready() noexcept { return false; }
        void await_suspend(std::coroutine_handle<> handle) noexcept
        {
            if (!set)
            {
                auto ptr = handle.address();
                auto event = xp::make_epoll_event(epoll_data_t{.ptr = ptr});
                loop->ctl(xp::EventLoop::add, fd, &event);
                set = true;
            }
        }
        constexpr void await_resume() noexcept {}
        ~EventAwaiter()
        {
            log();
            loop->ctl(xp::EventLoop::del, fd, nullptr);
        }
    };

    using ReadTask = BasicTask<AwaitedPromise>;
    ReadTask co_read(int fd, void *p, size_t num, bool &result)
    {
        xp::log();
        if (!p)
        {
            co_return;
        }
        char *buf = static_cast<char *>(p);
        xp::EventAwaiter awaiter{&accept_loop, fd, false};
        while (num)
        {
            auto read_result = ::recv(fd, buf, num, MSG_DONTWAIT);
            log(fmt::format("read_result={}",read_result),"info");
            if (read_result == num) [[likely]]
            {
                log();
                result = true;
                co_return;
            }
            else if (read_result <= 0)
            {
                xp::log(fmt::format("errno={}", errno), "debug");
                if (errno == EINTR || errno == EWOULDBLOCK || errno == EAGAIN)
                {
                    log();
                    co_await awaiter;
                }
                else
                {
                    log();
                    result = false;
                    co_return;
                }
            }
            else if (read_result < num)
            {
                log();
                num -= read_result;
                buf += read_result;
                co_await awaiter;
            }
        }
        log();
        result = true;
        co_return;
    }

    using WriteTask = BasicTask<AwaitedPromise>;
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

    using WriteReuseTask = BasicTask<BasicPromise>;
    WriteReuseTask co_write_reuse(const int fd, char *&buf, size_t &num, int &output_result)
    {
        xp::log();
        co_await std::suspend_always{};
        while (buf)
        {
            auto result = ::send(fd, buf, num, MSG_DONTWAIT);
            xp::log(fmt::format("send result={}", result));

            if (result == num) [[likely]]
            {
                num -= result;
                output_result = 1;
                co_await std::suspend_always{};
            }
            else if (result <= 0)
            {
                xp::log(fmt::format("errno={}", errno));
                if (errno == EINTR || errno == EWOULDBLOCK || errno == EAGAIN)
                {
                    output_result = 0;
                    co_await std::suspend_always{};
                }
                else
                {
                    output_result = -1;
                    co_await std::suspend_always{};
                }
            }
            else if (result < num)
            {
                num -= result;
                buf += num;
                output_result = 0;
                co_await std::suspend_always{};
            }
        }
        xp::log();
        co_return;
    }
    
    
}

#endif // !CO_NET_H