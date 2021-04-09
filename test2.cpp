#include <iostream>
#include "logger.h"
#include "co.hpp"
#include "co_net.h"
#include "event_manager.h"
#include "server.h"
#include "co_sched.h"

using namespace std;
using namespace xp;

thread_local int xp::to_del_fd;
thread_local epoll_event thread_epoll_event;


xp::Scheduler xp::Singleton<xp::Scheduler>::instance_;
auto sched = &xp::Singleton<xp::Scheduler>::get();

int sleep_time = 1;

auto main_event_handler = [](epoll_event epevent) {
    xp::log();
    sched->pool.add_task([epevent]() {
        sched->event_handler(epevent);
    });
};

auto accept_event_handler = [](epoll_event epevent) {
    xp::log();
    auto handle = std::coroutine_handle<>::from_address(epevent.data.ptr);
    thread_epoll_event = epevent;
    handle.resume();
};

xp::EventLoop main_loop{main_event_handler};

xp::EventLoop accept_loop{accept_event_handler};

xp::Server xp::Singleton<xp::Server>::instance_;
auto server = &xp::Singleton<xp::Server>::get();

//Logger logger;

int main()
{
    /*auto f = std::async(std::launch::deferred, &xp::EventLoop::start, &loop, -1);
    f.wait();
    */

    main_loop.start(-1);
    sleep(5);
}