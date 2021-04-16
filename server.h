#ifndef SERVER_H_
#define SERVER_H_

#include "event_manager.h"
#include "net.h"
#include "co_net.h"
#include "co.hpp"
#include <bit>
#include "net_type.h"
#include <list>
#include <mutex>

namespace xp
{
	class Server;
}

extern xp::Server *server;
extern xp::EventLoop main_loop;
extern xp::EventLoop accept_loop;

namespace xp
{

	class Server
	{
	public:
		friend class xp::Singleton<Server>;
		const xp::roomid_type default_roomid = 10086;
		std::vector<xp::roomid_type> default_room{default_roomid};
		/////////////
		Server();
		~Server();
		BasicTask<> co_accept(const int fd);
		User *try_login(xp::Connection *conn, xp::userid_type id, xp::userpassword_type password);
		bool handle_message(xp::MessageWrapper msg_wrapper, xp::Connection *from_conn = nullptr);
		void del_conn(int fd);
		xp::Room *get_room(xp::roomid_type id);
		xp::Acceptor acceptor_;
		xp::BasicTask<> accept_task_;

	private:
		void init_users();
		void init_rooms();
		std::unordered_map<int, std::unique_ptr<Connection>> connections_;
		std::unordered_map<xp::userid_type, std::unique_ptr<xp::User>> users_;
		std::unordered_map<xp::roomid_type, std::unique_ptr<xp::Room>> rooms_;
		std::shared_mutex conn_mtx_;
		std::shared_mutex users_mtx_;
		std::shared_mutex rooms_mtx_;
	};

	using ConnTask = AutoDestroyTask<FinalSuspendPromise>;

	struct Connection
	{
		int fd;
		sockaddr_in addr;
		xp::ConnTask task;
		xp::User *user;
		std::list<xp::MessageWrapper> messages;
		//std::mutex msg_lock;
		xp::SpinLock msg_lock;
		bool state = true;

		Connection(int f, sockaddr_in a);

		Connection(const Connection &) = delete;
		Connection &operator=(const Connection &) = delete;
		Connection(Connection &&conn) noexcept
			: fd{conn.fd}, addr{conn.addr}, user{conn.user},
			  messages{std::move(conn.messages)}, msg_lock{}, state{conn.state}
		{
		}
		Connection &operator=(Connection &&conn) noexcept
		{
			fd = conn.fd;
			addr = conn.addr;
			user = conn.user;
			messages = std::move(conn.messages);
			state = conn.state;
			return *this;
		}
		~Connection()
		{
			if (fd >= 0) [[likely]]
			{
				::close(fd);
			}
		}
		auto messages_size() const noexcept
		{
			return messages.size();
		}
		bool is_messages_empty() const noexcept
		{
			return messages.empty();
		}
		xp::MessageWrapper get_message()
		{
			log();
			std::lock_guard lg{msg_lock};
			//log();
			log(fmt::format("messages size={}", messages.size()), "info");
			auto msg = messages.front();
			log(fmt::format("from_id={}", xp::hton(msg.get()->from_id)), "info");
			messages.pop_front();
			return msg;
		}
		// &
		void add_message(xp::MessageWrapper msg)
		{
			log();
			std::lock_guard lg{msg_lock};
			//log();
			std::cout << "messages.size():" << messages.size() << std::endl;
			messages.push_back(msg);
			//messages.emplace_back(std::move(msg));
			log();
			if (messages.size() == 1)
			{
				log();
				auto handle = task.handle;
				auto ts = [this, handle] {
					if (!this->is_messages_empty())
					{
						thread_epoll_event = epoll_event{0, epoll_data_t{.ptr = handle.address()}};
						main_loop.event_handler_(thread_epoll_event);
					}
				};
				main_loop.add_task(ts);
			}
		}
	};

	ConnTask co_connection(Connection *conn)
	{
		log();
		const int fd = conn->fd;
		const sockaddr_in addr = conn->addr;

		//delete conn from server
		xp::Defer defer1{[fd, conn] {
			conn->state = false;
			if (conn->user) [[likely]]
			{
				conn->user->state = false;
				conn->user->conn = nullptr;
			}
			main_loop.add_task([fd] { server->del_conn(fd); });
		}};

		xp::log("login module");
		//login module
		{
			constexpr int login_message_num = head_size + userpassword_size;
			char login_buf[login_message_num];
			//bool login_read_result = false;
			//co_await co_read(fd, login_buf, login_message_num, login_read_result);
			int res = ::recv(fd, login_buf, login_message_num, MSG_WAITALL);
			if (res != login_message_num) //if (!login_read_result)
			{
				log("loggin error", "error");
				co_return;
			}
			auto login_message = (Message *)login_buf;

			if (auto msg_type = login_message->msg_type;
				xp::ntoh(msg_type) == message_type::login)
			{
				log();
				xp::userpassword_type password;
				std::copy_n((char *)(&login_message->context), xp::userpassword_size, (char *)password);
				if (conn->user = server->try_login(conn, xp::ntoh(login_message->from_id), xp::ntoh(password));
					conn->user) [[likely]]
				{
					log("login success", "info");
					std::string login_msg = fmt::format("{} login success.", conn->user->id);
					//auto login_result_msg = make_msg(conn->fd, login_result, 0, server->default_roomid, login_msg);

					MessageWrapper login_result_msg{login_msg.size()};
					{
						auto msg = login_result_msg.get();

						msg->msg_type = xp::hton(login_result);
						msg->timestamp = xp::hton(std::time(0));
						msg->from_id = xp::hton(0);
						msg->to_id = xp::hton(10086);

						xp::context_size_type cs{login_msg.size()};
						msg->context_size = xp::hton(cs);

						std::copy_n(login_msg.data(), login_msg.size(), login_result_msg.context_data());
					}

					::send(fd, login_result_msg.data(), login_result_msg.size(), MSG_WAITALL);
					server->handle_message(login_result_msg, conn);
				}
				else
				{
					log();
					co_return;
				}
			}
			else
			{
				log();
				co_return;
			}
		}

		xp::log("co_await awaiter to sched");
		xp::EventAwaiter awaiter{&main_loop, fd};
		co_await awaiter;
		xp::log();

		//read
		xp::MessageWrapper read_message{0};

		xp::MessageWrapper message_head{0};
		char *read_ptr = message_head.data();
		size_t read_num = head_size;
		bool has_read_msg_head = false;

		//write
		xp::MessageWrapper write_message{0};
		char *write_ptr;
		size_t write_num = 0;
		int write_result = 0;
		auto reuse_write_task = xp::co_write_reuse(fd, write_ptr, write_num, write_result);

		bool flag = true;
		while (flag)
		{
			{
				log();
				sleep(sleep_time);

				int result = 0;
				int events = thread_epoll_event.events;
				xp::log(fmt::format("events=", events), "info");
				//ok
				if (events & EPOLLERR) [[unlikely]]
				{
					xp::log("EPOLLERR", "info");
					co_return;
				}
				//ok
				if (events & EPOLLRDHUP) [[unlikely]]
				{
					xp::log("EPOLLRDHUP", "info");
					co_return;
				}
				//ok
				if (events & EPOLLIN)
				{
					result = ::recv(fd, read_ptr, read_num, MSG_DONTWAIT);
					log(fmt::format("recv result={}", result));

					if (result == 0)
					{
						xp::log();
						co_return;
					}
					else if (result < 0)
					{
						log();
						if (!(errno == EINTR || errno == EAGAIN))
						{
							xp::log();
							errno = 0;
							co_return;
						}
						log(fmt::format("erron={}", errno), "info");
					}
					else if (result == read_num)
					{
						xp::log();
						if (has_read_msg_head = !has_read_msg_head;
							has_read_msg_head)
						{
							const auto context_size = message_head.context_size();
							log(fmt::format("context_size={}", context_size));
							read_message = xp::MessageWrapper{context_size};
							std::copy_n(message_head.data(), head_size, read_message.data());
							read_ptr = read_message.context_data();
							read_num = context_size;
						}
						else
						{
							if (!server->handle_message(std::move(read_message)))
							{
								xp::log();
								co_return;
							}
							read_ptr = message_head.data();
							read_num = head_size;
						}
						continue;
					}
					else // 0 < result < read_num
					{
						xp::log();
						read_num -= result;
						read_ptr += result;
					}
				}

				//imcomplete!!!
				//has old msg to complete or has new msg to begin
				while (events & EPOLLOUT || !conn->is_messages_empty())
				{
					log();
					//if msg has completed, get new msg
					if (!(events & EPOLLOUT) && !conn->is_messages_empty())
					{
						log();
						write_message = std::move(conn->get_message());
						write_ptr = write_message.data();
						write_num = write_message.size();
					}
					reuse_write_task.resume();
					log(fmt::format("write_result={}", write_result), "info");
					if (write_result == 1) [[likely]] //complete
					{
						log();
						//if msg has completed, get new msg
						if (!conn->is_messages_empty())
						{
							log();
							while (1)
							{
								log();
								write_message = std::move(conn->get_message());
								if (xp::ntoh(write_message.get()->from_id) !=
									conn->user->id)
								{
									break;
								}
							}
							write_ptr = write_message.data();
							write_num = write_message.context_size();
							continue;
						}
						else if (events & EPOLLOUT) //set unoutable
						{
							log("set unoutable");
							auto epevent = thread_epoll_event;
							epevent.events &= ~EPOLLOUT;
							main_loop.commit_ctl(xp::EventLoop::ctl_option::mod, fd, epevent);
							break;
						}
					}
					else if (write_result == 0) //imcomplete
					{
						log("set outable");
						//set outable
						auto epevent = thread_epoll_event;
						epevent.events &= EPOLLOUT;
						main_loop.commit_ctl(xp::EventLoop::ctl_option::mod, fd, epevent);
						break;
					}
					else if (write_result == -1) //error
					{
						log("write error");
						co_return;
					}
				}
			}
			log("suspend");
			co_await std::suspend_always{};
		}

		log();
		co_return;
	}

	Connection::Connection(int f, sockaddr_in a)
		: fd{f}, addr{a}, task{xp::co_connection(this)},
		  user{nullptr}, messages{}, msg_lock{}
	{
	}

	Server::Server()
	{
		log();
		acceptor_.listen();
		accept_task_ = co_accept(acceptor_.fd);
		auto accept_event = xp::make_epoll_event(epoll_data_t{.ptr = accept_task_.handle.address()});
		accept_loop.ctl(EventLoop::add, acceptor_.fd, &accept_event);

		init_users();
		init_rooms();
	}

	void Server::init_users()
	{
		log();
		//users_[2385] = std::make_unique<xp::User>(2385, "crh");
		//users_[2386] = std::make_unique<xp::User>(2386, "fy");
		users_[2387] = std::make_unique<xp::User>(2387, "mahou");
		users_[2388] = std::make_unique<xp::User>(2388, "xp");
	}

	void Server::init_rooms()
	{
		log(); //log
		rooms_[default_roomid] = std::make_unique<xp::Room>(default_roomid);
		auto &users = rooms_[default_roomid]->users;
		for (auto &p : users_)
		{
			auto id = p.first;
			auto user = p.second.get();

			xp::log(fmt::format("id={}", id)); //log
			users.emplace(std::make_pair(id, user));
		}
	}

	Server::~Server()
	{
		log(); //log
	}

	//imcomplete
	xp::User *Server::try_login(xp::Connection *conn, xp::userid_type id, xp::userpassword_type password)
	{
		log();

		std::shared_lock lg{users_mtx_};

		if (auto iter = users_.find(id);
			iter != users_.end()) [[likely]]
		{
			log();
			auto user = iter->second.get();
			user->state = true;
			user->conn = conn;

			return user;
		}
		else
		{
			log();
			return nullptr;
		}
	}
	//imcomplete
	bool Server::handle_message(xp::MessageWrapper msg_wrapper, xp::Connection *from_conn)
	{
		log("handle message");
		auto msg = msg_wrapper.get();
		auto from_id = xp::ntoh(msg->from_id);
		auto to_id = xp::ntoh(msg->to_id);
		switch (xp::ntoh(msg->msg_type))
		{
		case xp::message_type::msg:
		{
			log();
			auto context = std::string_view{msg->context, xp::ntoh(msg->context_size)};
			auto from_id = xp::ntoh(msg->from_id);
			auto to_id = xp::ntoh(msg->to_id);
			log(fmt::format("from {} to {} says : {}", from_id, to_id, context), "info");

			if (auto room = this->get_room(to_id); room) [[likely]]
			{
				for (auto &userdata : room->users)
				{
					auto user = userdata.second;
					log(fmt::format("online userid : ", user->id), "info");
					if (user->conn && from_id != user->id)
					{
						log(fmt::format("add to {}", user->id), "info");
						user->conn->add_message(msg_wrapper);
					}
				}
				return true;
			}
			break;
		}
		case xp::message_type::login_result:
		{
			log();
			auto context = std::string_view{msg->context, xp::ntoh(msg->context_size)};
			auto from_id = xp::ntoh(msg->from_id);
			auto to_id = xp::ntoh(msg->to_id);
			log(fmt::format("from {} to {} says : {}", from_id, to_id, context), "info");

			if (auto room = this->get_room(to_id); room) [[likely]]
			{
				for (auto &userdata : room->users)
				{
					auto user = userdata.second;
					if (user->conn && from_conn->user->id != user->id)
					{
						log(fmt::format("online userid : ", user->id), "info");
						log(fmt::format("add to {}", user->id), "info");
						user->conn->add_message(msg_wrapper);
					}
				}
				return true;
			}
			break;
		}
		case xp::message_type::logout:
		{
			log();
			break;
		}
		}
		return false;
	}

	BasicTask<> Server::co_accept(const int lisent_fd)
	{
		xp::log();
		co_await std::suspend_always{};
		while (true)
		{
			sockaddr_in addr;
			socklen_t len = sizeof(addr);
			if (int fd = ::accept(lisent_fd, (sockaddr *)&addr, &len);
				fd >= 0)
			{
				xp::log(fmt::format("accept fd={}", fd));
				if (auto conn = std::make_unique<Connection>(fd, addr);
					conn->state) [[likely]]
				{
					log("add conn to server", "info");
					std::unique_lock lg{conn_mtx_};
					connections_[fd] = std::move(conn);
				}
			}
			else
			{
				co_await std::suspend_always{};
			}
		}
		co_return;
	}

	//ok, auto lock
	void Server::del_conn(int fd)
	{
		log("delete conn from server", "info");

		std::unique_lock lg{conn_mtx_};
		if (auto iter = connections_.find(fd);
			iter != connections_.end())
		{
			connections_.erase(iter);
		}
	}

	//ok
	xp::Room *Server::get_room(xp::roomid_type id)
	{
		std::shared_lock lg{rooms_mtx_};
		return rooms_[id].get();
	}

	/////////////////////////////////////////////

}

#endif