#include <iostream>
#include "logger.h"
#include "co.hpp"
#include "co_net.h"
#include "event_manager.h"
#include "server.h"

using namespace std;
using namespace xp;

thread_local int xp::to_del_fd;
thread_local epoll_event th_epevent;

int sleep_time = 2;

xp::EventLoop::event_handler_type event_handler = [](epoll_event epevent) {
    xp::log();
    auto handle = std::coroutine_handle<>::from_address(epevent.data.ptr);
    th_epevent = epevent;
    xp::to_del_fd = -1;
    handle.resume();
    if (handle.done())
    {
        xp::log();
        server->close_conn(xp::to_del_fd);
    }
    else
    {
        xp::log();
    }
};

xp::EventLoopManager loops(5, event_handler);
xp::EventLoop &accept_loop = loops[0];
xp::Server sss_{};
xp::Server *server = &sss_;

Logger logger;

int main()
{
    /*
    auto f = std::async(std::launch::deferred, &xp::EventLoop::start, &loop, -1);
    f.wait();
    */

    auto &main_loop = loops[0];

    main_loop.start(-1);
    sleep(5);
}