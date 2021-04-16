#include <iostream>
#include "logger.h"
#include "co.hpp"
#include "co_net.h"
#include "event_manager.h"
#include "server.h"
#include "co_sched.h"

using namespace std;
using namespace xp;

//Logger logger;
//template <> xp::Logger xp::Singleton<Logger>::instance_;

xp::Logger llgg;
xp::Logger *logger = &llgg;
xp::Server *server;

int sleep_time = 0;
thread_local int xp::to_del_fd;

thread_local epoll_event thread_epoll_event;

//template<> xp::Scheduler xp::Singleton<xp::Scheduler>::instance_;
//auto sched = &xp::Singleton<xp::Scheduler>::get();

auto main_event_handler = [](epoll_event epevent) {
	xp::log("main_loop_event");
	//sched->pool.add_task([epevent] { sched->event_handler(epevent); });
	sched->event_handler(epevent);
	std::cout << "handles_num: " << sched->coro_states.size() << std::endl;
};

auto accept_event_handler = [](epoll_event epevent) {
	xp::log("accept_loop_event");
	auto handle = std::coroutine_handle<>::from_address(epevent.data.ptr);
	thread_epoll_event = epevent;
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

		log("add to sched");
		sched->coro_states[main_loop_wakeup_task.handle.address()] =
			std::make_unique<xp::CoroState>(main_loop_wakeup_task.handle);

		std::jthread accept_thread{&xp::EventLoop::start, &accept_loop};
		main_loop.start();
	}
	catch (...)
	{
		run = false; 
		cout << "------end-------\n";
	}
}