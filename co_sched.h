#ifndef XP_CO_SCHED_H_
#define XP_CO_SCHED_H_

#include "event_manager.h"

#include <map>
#include <unordered_map>
#include <mutex>
#include <memory>
#include <atomic>

extern xp::EventLoop main_loop;
extern thread_local epoll_event thread_epoll_event;


namespace xp
{
	struct CoroState
	{
		std::coroutine_handle<> handle = std::noop_coroutine();
		std::atomic<bool> running = false;
		std::atomic<bool> can_resume = true;
		xp::SpinLock owning;
		std::mutex run_mtx;
	};

	struct Scheduler
	{
		std::unordered_map<void*, std::unique_ptr<xp::CoroState>> coro_states;
		std::shared_mutex coro_states_mtx;
		xp::ThreadPool pool;

		void event_handler(epoll_event epevent)
		{
			auto handle = std::coroutine_handle<>::from_address(epevent.data.ptr);
			xp::CoroState *state;
			{
				std::shared_lock sl{coro_states_mtx};
				auto p = coro_states.find(handle.address());
				log();
				if (p != coro_states.end()) [[likely]]
				{
					log();
					state = p->second.get();
				}
				else
				{
					return;
				}
			}
			if (state->owning.try_lock()) [[likely]]
			{
				if (state->can_resume) [[likely]]
				{
					thread_epoll_event = epevent;
					handle.resume();
					if (handle.done()) [[unlikely]]
					{
						xp::log();
					}
				}
				state->owning.unlock();
			}
		}

		
	};
}
//template<> xp::Scheduler xp::Singleton<xp::Scheduler>::instance_;
xp::Scheduler sched__;
inline xp::Scheduler *sched = &sched__;
//&xp::Singleton<xp::Scheduler>::get();

#endif // !XP_CO_SCHED_H_
