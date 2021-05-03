#include <iostream>
#include "logger.h"
#include "co.hpp"
#include "co_net.h"
#include "event_manager.h"
#include "server.h"
#include "co_sched.h"

using namespace std;
using namespace xp;

// add // user(room) -> corostate(sched) -> ctl(eventloop) -> conn(room)

// del // user(room) -> corostate(sched) -> ctl(eventloop) -> conn(room)


//Logger logger;
//template <> xp::Logger xp::Singleton<Logger>::instance_;
thread_local std::vector<char> local_recv_buf(1024);
xp::Logger llgg;
xp::Logger *logger = &llgg;
xp::Server *server;

int sleep_time = 0;

thread_local epoll_event local_epoll_event;

//
//template<> xp::Scheduler xp::Singleton<xp::Scheduler>::instance_;
//auto sched = &xp::Singleton<xp::Scheduler>::get();

auto main_event_handler = [](epoll_event epevent) {
	xp::log("main loop event");
	sched->event_handler(epevent);
};

auto accept_event_handler = [](epoll_event epevent) {
	xp::log("accept loop event");
	auto handle = std::coroutine_handle<>::from_address(epevent.data.ptr);
	local_epoll_event = epevent;
	handle.resume();
};

xp::EventLoop main_loop{main_event_handler};

xp::EventLoop accept_loop{accept_event_handler};

//xp::Server xp::Singleton<xp::Server>::instance_;
xp::Server ss__;
//&xp::Singleton<xp::Server>::get();


int main()
{
	bool run = true;
	jthread jlog{[&run] {
		while (run)
		{
			logger->output();
			sleep(3);
		}
		logger->output();
		run = true;
	}};

	try
	{
		server = &ss__;
		auto accept_loop_wakeup_task = xp::co_wakeup(accept_loop.fd());
		auto accept_event = xp::make_epoll_event(epoll_data_t{.ptr = accept_loop_wakeup_task.handle.address()});
		accept_loop.ctl(EventLoop::add, accept_loop.fd(), &accept_event);

		auto main_loop_wakeup_task = xp::co_wakeup(main_loop.fd());
		auto main_event = xp::make_epoll_event(epoll_data_t{.ptr = main_loop_wakeup_task.handle.address()});
		main_loop.ctl(EventLoop::add, main_loop.fd(), &main_event);

		sched->add_coro_state(main_loop_wakeup_task.handle.address());

		std::jthread accept_thread{&xp::EventLoop::start, &accept_loop};
		main_loop.start();
	}
	catch (...)
	{
		run = false;
		while(!run)
			continue;
		logger->output();
		cout << "------end-------\n";
	}
}