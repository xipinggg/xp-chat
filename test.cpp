#include <iostream>
#include "logger.h"
#include "co.hpp"
#include "co_net.h"
#include "event_manager.h"

using namespace std;
using namespace xp;

std::coroutine_handle<> g_handle;
int sleep_time = 1;

struct EventAwaiter
{
    bool await_ready() noexcept { return false; }
    void await_suspend(std::coroutine_handle<> handle) noexcept
    {
        g_handle = handle;
    }
    void await_resume() noexcept {}
};

xp::EventLoop::event_handler_type event_handler = [](epoll_event epevent) {
    xp::log();
    auto handle = std::coroutine_handle<>::from_address(epevent.data.ptr);
    handle.resume();
    return -1;
};

xp::EventLoop loop{event_handler, [](int fd) { return xp::co_wakeup(fd).handle.address(); }};
struct Server
{
    xp::Acceptor acceptor;
};

Server server;

int main()
{
    std::async(std::launch::async, &xp::EventLoop::start, &loop, -1);
    server.acceptor.co_accept();
}