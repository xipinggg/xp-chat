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
#include <sys/uio.h>
#include "logger.h"
#include "co.hpp"
#include "event_manager.h"
#include "co_sched.h"
#include "net_type.h"
extern thread_local epoll_event local_epoll_event;
extern int sleep_time;

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
		uint32_t events = default_epoll_events;
		// delete fd from loop
		// delete corotinue from shed
		~EventAwaiter()
		{
			if (set && loop) [[likely]]
			{
				log("delete coro_state from shed, delete fd from loop", "info");
				sched->del_coro_state(this_handle.address());
				loop->ctl(xp::EventLoop::del, fd, nullptr);
			}
		}
		constexpr bool await_ready() noexcept { return false; }
		// add fd to loop,add corotinue to shed
		void await_suspend(std::coroutine_handle<> handle) noexcept
		{
			this_handle = handle;
			if (!set && loop)
			{
				log("add coro_state to shed, add fd to loop", "info");
				set = true;
				sched->add_coro_state(handle.address());
				auto event = xp::make_epoll_event(epoll_data_t{.ptr = handle.address()}, events);
				loop->ctl(xp::EventLoop::add, fd, &event);
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
		xp::EventAwaiter awaiter{&main_loop, fd};
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






	struct ReadAlwaysData
	{
		int fd = -1;
		std::vector<xp::MessageWrapper> msgs{};
		std::vector<char> &buf;
		int result{};
		std::function<std::tuple<char *, size_t>(std::vector<xp::MessageWrapper> &, char *, size_t)> parser;
		bool is_error()
		{
			return result == -1;
		}
		void set()
		{

		}
	};

	using ReadTask = BasicTask<AwaitedPromise>;
	ReadTask co_read_always(ReadAlwaysData &data)
	{
		xp::log();

		int fd = data.fd;
		int &result = data.result;
		auto parser = data.parser;
		auto &buf = data.buf;
		size_t max_num = data.buf.size();

		auto ptr = buf.data();
		co_await std::suspend_always{};
		while (true)
		{
			auto read_result = ::recv(fd, ptr, max_num, MSG_DONTWAIT);
			log(fmt::format("read_always recv result={}", read_result), "info");

			if (read_result == max_num) [[unlikely]]
			{
				log();
				result = 2;
			}
			else if (read_result == 0)
			{
				xp::log(fmt::format("errno={}", errno), "debug");
				result = -1;
			}
			else if (read_result < 0)
			{
				xp::log(fmt::format("errno={}", errno), "debug");
				if (errno == EINTR || errno == EWOULDBLOCK || errno == EAGAIN)
				{
					log();
					result = 0;
				}
				else
				{
					log();
					result = -1;
					co_return;
				}
			}
			else if (read_result < max_num)
			{
				log();
				if (auto buf_num = ptr - buf.data() + read_result; buf_num > head_size)
				{
					data.msgs.clear();
					auto tp = parser(data.msgs, buf.data(), buf_num);
					auto temp_ptr = std::get<0>(tp);
					const auto temp_num = std::get<1>(tp);
					std::vector<char> temp_buf(temp_num);
					std::copy_n(temp_ptr, temp_num, temp_buf.data());
					co_await std::suspend_always{};
					std::copy_n(temp_buf.data(), temp_num, buf.data());
					ptr = buf.data() + temp_num;
					max_num = buf.size() - temp_num;
					continue;
				}
			}

			co_await std::suspend_always{};
		}
		log();
		co_return;
	}

	struct WritevData
	{
		int fd = -1;
		iovec *ptr {};
		int count {};
		size_t size {};
		int result = 1;
		void set(iovec *p, int cnt, size_t s)
		{
			ptr = p;
			count = cnt;
			size = s;
		}
		bool is_complete()
		{
			return result == 1;
		}
		bool is_error()
		{
			return result == -1;
		}
	};

	using WritevTask = BasicTask<BasicPromise>;
	WritevTask co_writev(WritevData &data)
	{
		auto fd = data.fd;
		auto &ptr = data.ptr;
		auto &iov_count = data.count;
		auto &total_size = data.size;
		auto &output_result = data.result;
		xp::log();
		co_await std::suspend_always{};
		while (ptr)
		{
			auto result = ::writev(fd, ptr, iov_count);
			xp::log(fmt::format("co_writev send result={}", result));

			if (result == total_size) [[likely]]
			{
				total_size = 0;
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
			else if (result < total_size)
			{
				total_size -= result;
				auto temp_result = result;
				while (true)
				{
					if (temp_result >= ptr->iov_len)
					{
						temp_result -= ptr->iov_len;
						++ptr;
						--iov_count;
					}
					else
					{
						ptr->iov_base = static_cast<char *>(ptr->iov_base) + temp_result;
						break;
					}
				}
				output_result = 0;
				co_await std::suspend_always{};
			}
		}
		xp::log();
		co_return;
	}
}

#endif // !CO_NET_H