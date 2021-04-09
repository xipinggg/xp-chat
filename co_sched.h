#ifndef XP_CO_SCHED_H_
#define XP_CO_SCHED_H_

#include "co.hpp"
#include "event_manager.h"
#include "server.h"
#include <map>
#include <unordered_map>
#include <mutex>
#include <memory>
#include <atomic>

extern xp::EventLoop main_loop;

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
		std::unordered_map<std::coroutine_handle<>, std::unique_ptr<CoroState>> coro_states;
		std::shared_mutex coro_states_mtx;
		xp::ThreadPool pool;
		
		void event_handler(epoll_event epevent)
		{
			auto handle = std::coroutine_handle<>::from_address(epevent.data.ptr);
			xp::CoroState *state;
			{
				std::shared_lock sl{coro_states_mtx};
				auto p = coro_states.find(handle);
				if (p != coro_states.end()) [[likely]]
				{
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
						if (xp::to_del_fd >= 0)
						{
							server->del_conn(xp::to_del_fd);
						}
					}
					else
					{
						xp::log();
					}
				}
				state->owning.unlock();
			}
		}

		
	};
}

#endif // !XP_CO_SCHED_H_
