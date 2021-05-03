#ifndef EVENT_MANAGER_H_
#define EVENT_MANAGER_H_

#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <sys/eventfd.h>
#include <cassert>
#include <unistd.h>

#include <vector>
#include <iostream>
#include <span>
#include <mutex>
#include <exception>
#include <stdexcept>
#include <functional>

#include "logger.h"

extern int sleep_time;
namespace xp
{
	enum : uint32_t
	{
		default_epoll_events = EPOLLET | EPOLLIN | EPOLLPRI | EPOLLHUP
	};
	epoll_event make_epoll_event(const epoll_data_t data,
								 const uint32_t events = default_epoll_events)
	{
		epoll_event event;
		event.events = events;
		event.data = data;
		/*
			*    void *ptr;
			*    int fd;
			*    uint32_t u32;
			*    uint64_t u64;
			*/
		return event;
	}

	//movable
	class Epoller
	{
	public:
		Epoller() : epollfd_{epoll_create1(EPOLL_CLOEXEC)}
		{
			log(fmt::format("epollfd={}", epollfd_));
			if (epollfd_ < 0)
				throw std::bad_exception{};
		}
		Epoller(const Epoller &) = delete;
		Epoller &operator=(const Epoller &) = delete;
		Epoller(Epoller &&e) noexcept
			: epollfd_{e.epollfd_}, events_{std::move(e.events_)}
		{
			e.epollfd_ = -1;
		}
		Epoller &operator=(Epoller &&e) noexcept
		{
			epollfd_ = e.epollfd_;
			events_ = std::move(e.events_);

			e.epollfd_ = -1;
			return *this;
		}
		~Epoller()
		{
			log(fmt::format("close epollfd : {}", epollfd_));
			if (epollfd_ >= 0)
				::close(epollfd_);
		}
		int ctl(const int option, const int fd, epoll_event *event)
		{
			log(fmt::format("option={},fd={}", option, fd));
			return ::epoll_ctl(epollfd_, option, fd, event);
		}
		auto epoll(std::vector<epoll_event> &events, const int timeout = 0)
		{
			log();
			auto *buf = events.data();
			assert(buf);
			const int max_num = events.capacity();
			assert(max_num > 0);
			return epoll_wait(epollfd_, buf, max_num, timeout);
		}
	private:
		int epollfd_;
		std::vector<epoll_event> events_;
	};
/*
	class Timer
	{
	public:
		friend class TimerManager;
		Timer(const int clockid = CLOCK_REALTIME)
			: fd_{timerfd_create(clockid, TFD_CLOEXEC | TFD_NONBLOCK)}
		{
			log();
			if (fd_ < 0) [[unlikely]]
				throw std::bad_exception{};
		}
		~Timer()
		{
			log();
			::close(fd_);
		}
		int set(const int flags, const itimerspec *new_value, itimerspec *old_value = nullptr) noexcept
		{
			return timerfd_settime(fd_, flags, new_value, old_value);
		}
		int set_absolute(const itimerspec *new_value, itimerspec *old_value = nullptr) noexcept
		{
			return timerfd_settime(fd_, TFD_TIMER_ABSTIME, new_value, old_value);
		}
		int set_relative(const itimerspec *new_value, itimerspec *old_value = nullptr) noexcept
		{
			return timerfd_settime(fd_, 0, new_value, old_value);
		}
		int get(itimerspec *old_value) noexcept
		{
			return timerfd_gettime(fd_, old_value);
		}

	private:
		int fd_;
	};
	class TimerManager
	{
	public:
		TimerManager() {}
		~TimerManager() {}

	private:
	};
*/
	//movable
	class EventLoop
	{
		enum
		{
			invalid_fd = -1,
		};

	public:
		using task_type = std::function<void()>;
		using event_handler_type = std::function<void(epoll_event)>;
		enum ctl_option
		{
			add = EPOLL_CTL_ADD,
			del = EPOLL_CTL_DEL,
			mod = EPOLL_CTL_MOD,
		};

		EventLoop(event_handler_type event_handler)
			: fd_{eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK)}, event_handler_{event_handler}
		{
			log(fmt::format("eventfd={}", fd_));
			if (fd_ == invalid_fd)
			{
				throw std::bad_exception{};
			}
		}
		EventLoop(const EventLoop &) = delete;
		EventLoop &operator=(const EventLoop &) = delete;
		EventLoop(EventLoop &&e) noexcept
			: fd_{e.fd_}, events_{std::move(e.events_)}, tasks_{std::move(e.tasks_)},
			  epoller_{std::move(e.epoller_)}, event_handler_{e.event_handler_}
		{
			e.fd_ = invalid_fd;
		}
		EventLoop &operator=(EventLoop &&e) noexcept
		{
			fd_ = e.fd_;
			events_ = std::move(e.events_);
			tasks_ = std::move(tasks_);
			epoller_ = std::move(e.epoller_);
			event_handler_ = e.event_handler_;

			e.fd_ = invalid_fd;
			return *this;
		}
		~EventLoop()
		{
			if (fd_ >= 0)
				::close(fd_);
		}
		void start()
		{
			int timeout = -1;
			log();
			std::vector<epoll_event> events(1024);
			events.clear();
			while (true)
			{
				//sleep(sleep_time);
				/*struct timeval tv;
    			gettimeofday(&tv, NULL);
				log(fmt::format("end s={}",tv.tv_sec), "info");
				log(fmt::format("end ms={}",tv.tv_usec / 1000), "info");*/
				int num = epoller_.epoll(events, timeout);
				/*gettimeofday(&tv, NULL);
				log(fmt::format("start s={}",tv.tv_sec), "info");
				log(fmt::format("start ms={}",tv.tv_usec / 1000), "info");*/

				log(fmt::format("epoll result : {}", num), "info");
				for (int i = 0; i < num; ++i)
				{
					auto epevent = events[i];
					event_handler_(epevent);
				}
				do_tasks();
				
			}
		}

		int ctl(const ctl_option option, const int fd, epoll_event *event)
		{
			return epoller_.ctl(option, fd, event);
		}
		int ctl(const ctl_option option, const int fd, epoll_event event)
		{
			return ctl(option, fd, &event);
		}
		void commit_ctl(const ctl_option option, const int fd, epoll_event event)
		{
			log("commit ctl");
			auto task = [option, fd, event, this] {
				auto e = event;
				this->ctl(option, fd, &e); 
			};
			add_task(task);
		}
		void add_task(std::function<void()> task)
		{
			log("add task","info");
			tasks_.add(std::move(task));
			const auto cnt = wakeup_count_.fetch_add(1);
			log(fmt::format("wakeup_count={}", cnt),"info");
			if (cnt == 0)
			{
				wakeup();
			}
		}

		bool wakeup() noexcept
		{
			xp::log();
			eventfd_t value = 1;
			return eventfd_write(fd_, value) >= 0;
		}
		int fd() const noexcept { return fd_; }

	private:
		void do_tasks()
		{
			const auto count = wakeup_count_.exchange(0);
			log(fmt::format("wakeup_count_={}", count), "info");

			auto tasks = tasks_.get();
			
			for (auto task : tasks)
			{
				task();
			}
		}

	private:
		int fd_ = invalid_fd;
		std::vector<epoll_event> events_;
		xp::SwapBuffer<task_type> tasks_;
		xp::Epoller epoller_;
		std::atomic<int> wakeup_count_ {0};

	public:
		event_handler_type event_handler_;
	};

	class EventLoopManager
	{
	public:
		EventLoopManager(uint num, EventLoop::event_handler_type handler)
			: loops_num_{num}
		{
			while (num--)
			{
				loops_.emplace_back(handler);
			}
		}
		~EventLoopManager()
		{
		}
		EventLoop *select(const uint fd, uint &idx) noexcept
		{
			idx = fd % loops_num_;
			return &loops_[idx];
		}
		EventLoop *select(const int fd) noexcept
		{
			log();
			const int idx = fd % loops_num_;
			return &loops_[idx];
		}
		EventLoop &operator[](const int idx)
		{
			return loops_.at(idx);
		}

	private:
		uint loops_num_;
		std::vector<EventLoop> loops_;
		std::mutex mtx_;
	};

}

#endif