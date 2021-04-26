#ifndef XP_CO_SCHED_H_
#define XP_CO_SCHED_H_

#include "event_manager.h"

#include <map>
#include <unordered_map>
#include <mutex>
#include <memory>
#include <atomic>

extern xp::EventLoop main_loop;
extern thread_local epoll_event local_epoll_event;

namespace xp
{
	struct CoroState
	{
		bool get_own()
		{
			return owning.try_lock();
		}
		void release_own()
		{
			owning.unlock();
		}
		bool is_can_resume()
		{
			return can_resume;
		}
		void set_can_resume()
		{
			can_resume = true;
		}
		void set_can_not_resume()
		{
			can_resume = false;
		}

	private:
		std::atomic<bool> can_resume = true;
		xp::SpinLock owning;
	};

	class Scheduler
	{
		std::unordered_map<void *, std::unique_ptr<xp::CoroState>> coro_states;
		std::shared_mutex coro_states_mtx;
		xp::ThreadPool pool;
	public:
		xp::CoroState *get_coro_state(void *address)
		{
			std::shared_lock sl{coro_states_mtx};
			if (auto p = coro_states.find(address);
				p != coro_states.end()) [[likely]]
			{
				return p->second.get();
			}
			return nullptr;
		}
		void add_coro_state(void *address)
		{
			log("add_coro_state");
			std::unique_lock sl{coro_states_mtx};
			coro_states[address] = std::make_unique<CoroState>();
		}
		void del_coro_state(void *address)
		{
			log("del_coro_state");
			std::unique_lock sl{coro_states_mtx};
			if (auto iter = coro_states.find(address); iter != coro_states.end())
			{
				if (iter->second->get_own())
					coro_states.erase(iter);
			}
		}
		void event_handler(epoll_event epevent)
		{
			//auto handle = std::coroutine_handle<>::from_address(epevent.data.ptr);
			
			if (xp::CoroState *state = get_coro_state(epevent.data.ptr);
				state && state->get_own()) [[likely]]
			{
				if (state->is_can_resume()) [[likely]]
				{
					local_epoll_event = epevent;
					auto handle = std::coroutine_handle<>::from_address(epevent.data.ptr);
					handle.resume();
				}
				else
				{
					log("coro state can not resume", "error");
				}
				state->release_own();
			}
		}
	};
}
//template<> xp::Scheduler xp::Singleton<xp::Scheduler>::instance_;
xp::Scheduler sched__;
inline xp::Scheduler *sched = &sched__;
//&xp::Singleton<xp::Scheduler>::get();

#endif // !XP_CO_SCHED_H_
