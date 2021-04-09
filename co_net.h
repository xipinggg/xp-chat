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

extern thread_local epoll_event thread_epoll_event;
extern int sleep_time;
extern xp::Scheduler *sched;

extern thread_local int to_del_fd;
extern xp::EventLoop accept_loop;
extern xp::EventLoop main_loop;
namespace xp
{
    struct EventAwaiter
    {
        xp::EventLoop *const loop;
        const int fd;
        bool set = false;
        std::coroutine_handle<> this_handle = std::noop_coroutine();
        // delete fd from loop,delete corotinue from shed
        ~EventAwaiter()
        {
            log();
            if (set && loop) [[likely]]
            {
                loop->commit_ctl(xp::EventLoop::del, fd, {});
                auto handle = this->this_handle;
                loop->add_task([handle]() {
                    log("delete from sched", "info");
                    std::unique_lock ul{sched->coro_states_mtx};
                    sched->coro_states.erase(handle);
                });
            }
        }
        constexpr bool await_ready() noexcept { return false; }
        // add fd to loop,add corotinue to shed
        void await_suspend(std::coroutine_handle<> handle) noexcept
        {
            log();
            this_handle = handle;
            if (!set && loop)
            {
                set = true;
                const auto event = xp::make_epoll_event(epoll_data_t{.ptr = handle.address()});
                loop->commit_ctl(xp::EventLoop::add, fd, event);
                loop->add_task([handle]() {
                    log("add to sched", "info");
                    std::unique_lock ul{sched->coro_states_mtx};
                    sched->coro_states[handle] =
                        std::make_unique<xp::CoroState>(handle);
                });
            }
        }
        constexpr void await_resume() noexcept {}
    };

    using ReadTask = BasicTask<AwaitedPromise>;
    ReadTask co_read(int fd, void *p, size_t num, bool &result)
    {
        xp::log();
        if (!p) [[unlikely]]
        {
            co_return;
        }
        char *buf = static_cast<char *>(p);
        xp::EventAwaiter awaiter{&main_loop, fd, false};
        while (num)
        {
            auto read_result = ::recv(fd, buf, num, MSG_DONTWAIT);
            log(fmt::format("read_result={}", read_result), "info");
            if (read_result == num) [[likely]]
            {
                log();
                result = true;
                co_return;
            }
            else if (read_result == 0)
            {
                xp::log(fmt::format("errno={}", errno), "debug");
                result = false;
                co_return;
            }
            else if (read_result < 0)
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
            xp::log(fmt::format("co_write_reuse send result={}", result));

            if (result == num) [[likely]]
            {
                num -= result;
                output_result = 1;
                co_await std::suspend_always{};
            }
            else if (result == 0)
            {
                xp::log(fmt::format("errno={}", errno));
                output_result = -1;
                co_await std::suspend_always{};
            }
            else if (result < 0)
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