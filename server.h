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
#include <bitset>
namespace xp
{
	class Server;
}

extern xp::Server *server;
extern xp::EventLoop main_loop;
extern xp::EventLoop accept_loop;
extern thread_local std::vector<char> local_recv_buf;
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

		void resume_room_users(xp::Room *);
		User *try_login(xp::Connection *, xp::userid_type, xp::userpassword_type);
		bool handle_message(xp::MessageWrapper, xp::userid_type id = 0);

		void on_user_login(xp::User *);
		void on_user_logout(xp::User *);
		void on_conn_logout(xp::Connection *);
		xp::Connection *get_conn(int fd)
		{
			std::shared_lock ul{conn_mtx_};
			if (auto iter = connections_.find(fd); iter != connections_.end())
			{
				return iter->second.get();
			}
			return nullptr;
		}
		void add_conn(int fd, std::unique_ptr<xp::Connection> conn)
		{
			log(fmt::format("add conn {} to server", fd), "info");
			std::unique_lock ul{conn_mtx_};
			connections_[fd] = std::move(conn);
		}
		void del_conn(int fd)
		{
			log(fmt::format("delete conn {} from server", fd), "info");
			std::unique_lock lg{conn_mtx_};
			if (auto iter = connections_.find(fd); iter != connections_.end())
			{
				connections_.erase(iter);
			}
		}
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
		bool state;
		xp::User *user;
		xp::ConnTask task;

		Connection(int f, sockaddr_in a);
		~Connection()
		{
			log(fmt::format("close conn {}", fd), "info");
			if (fd >= 0) [[likely]]
				::close(fd);
		}
	};

	ConnTask co_connection(Connection *conn)
	{
		log();
		xp::Defer defer1{[conn] {
			main_loop.add_task([conn] { server->on_conn_logout(conn); });
		}};

		const int fd = conn->fd;
		const sockaddr_in addr = conn->addr;
		xp::log("login module");
		{
			constexpr int login_message_num = head_size + userpassword_size;
			char login_buf[login_message_num];
			int res = ::recv(fd, login_buf, login_message_num, MSG_WAITALL);
			if (res != login_message_num)
			{
				log("login error", "error");
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
					conn->state = true;
					std::string login_msg = fmt::format("{} login success.", conn->user->id);
					log(login_msg, "info"); //
					auto login_msg_wp = xp::make_message(login_result, 0, server->default_roomid, login_msg);
					server->handle_message(login_msg_wp, conn->user->id);
					::send(fd, login_msg_wp.data(), login_msg_wp.size(), MSG_WAITALL);
				}
				else
				{
					log("login fail", "info");
					co_return;
				}
			}
			else
			{
				log();
				co_return;
			}
		}

		conn->state = true;

		// conn -> online user -> loop fd -> sched

		xp::EventAwaiter awaiter{&main_loop, fd};

		auto defer2_user = conn->user;
		xp::Defer defer2{[defer2_user] {
			log();
			server->on_user_logout(defer2_user);
		}};
		xp::log("co_await awaiter to sched");
		co_await awaiter;
		log();
		//read
		xp::ReadAlwaysData read_data{.fd = conn->fd, .buf = local_recv_buf, .parser = xp::parse_recv_buf};
		auto read_task = xp::co_read_always(read_data);

		//write
		xp::WritevData writev_data{.fd = fd};
		std::vector<iovec> writev_iovec;
		auto writev_task = xp::co_writev(writev_data);

		bool flag = true;
		while (flag)
		{
			log();
			sleep(sleep_time);

			int result = 0;
			auto events = local_epoll_event.events;
			xp::log(fmt::format("events={}", events), "info");
			if (events & EPOLLERR || events & EPOLLRDHUP) [[unlikely]]
			{
				xp::log("EPOLLERR or EPOLLRDHUP", "info");
				co_return;
			}
			if (events & EPOLLIN)
			{
				log();
				read_task.resume();
				if (read_data.is_error())
				{
					log();
					co_return;
				}
				log(fmt::format("recv msgs size={}", read_data.msgs.size()), "info");
				for (auto &msg : read_data.msgs)
				{
					if (!server->handle_message(msg, conn->user->id))
						co_return;
				}
				read_data.msgs.clear();
			}

			while (events & EPOLLOUT || conn->user->has_msg())
			{
				log();
				if (writev_data.is_complete())
				{
					log();
					auto msgs = conn->user->get_msg();
					writev_iovec.reserve(msgs.size());
					writev_iovec.clear();
					size_t total_size = 0;
					for (auto &msg : msgs)
					{
						writev_iovec.push_back({msg.data(), msg.size()});
						total_size += msg.size();
					}
					writev_data.set(writev_iovec.data(), writev_iovec.size(), total_size);
				}
				//
				writev_task.resume();
				log(fmt::format("write_result={}", writev_data.result), "info");
				if (writev_data.is_error()) [[unlikely]] //error
				{
					log("writev error");
					co_return;
				}
				if (writev_data.is_complete()) [[likely]] //complete
				{
					log();
					if (conn->user->has_msg())
					{
						log();
						continue;
					}
					else if (events & EPOLLOUT) //set unoutable
					{
						log("set unoutable");
						auto epevent = local_epoll_event;
						epevent.events &= ~EPOLLOUT;
						main_loop.commit_ctl(xp::EventLoop::ctl_option::mod, fd, epevent);
						break;
					}
				}
				else //imcomplete
				{
					log("set outable");
					//set outable
					auto epevent = local_epoll_event;
					epevent.events &= EPOLLOUT;
					main_loop.commit_ctl(xp::EventLoop::ctl_option::mod, fd, epevent);
					break;
				}
			}
			log("suspend");
			co_await std::suspend_always{};
		}

		log("co_return");
		co_return;
	}

	Connection::Connection(int f, sockaddr_in a)
		: fd{f}, addr{a}, task{xp::co_connection(this)}
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
		users_[2385] = std::make_unique<xp::User>(2385, "crh");
		users_[2386] = std::make_unique<xp::User>(2386, "fy");
		users_[2387] = std::make_unique<xp::User>(2387, "mahou");
		users_[2388] = std::make_unique<xp::User>(2388, "xp");
		int id = 501;
		while (--id)
		{
			users_[id] = std::make_unique<xp::User>(id, "test");
		}
	}

	void Server::init_rooms()
	{
		log();
		rooms_[default_roomid] = std::make_unique<xp::Room>(default_roomid);
		for (auto &p : users_)
		{
			rooms_[default_roomid]->add_user(p.first, p.second.get());
		}
	}

	Server::~Server()
	{
		log();
	}

	void Server::on_conn_logout(xp::Connection *conn)
	{
		log();
		del_conn(conn->fd);
	}

	void Server::on_user_logout(xp::User *user)
	{
		int id = user->id;
		log("del outline user from room");
		if (!user)
		{	
			log("user invalid", "error");
		}
		if (auto room = get_room(default_roomid); room)
		{
			room->del_outline_user(user->id);
		}
		user->state = false;
		user->conn = nullptr;
		std::string logout_msg = fmt::format("{} logout.", id);

		auto logout_result_msg = xp::make_message(logout, 0, default_roomid, logout_msg);

		handle_message(logout_result_msg, id);
	}
	void Server::on_user_login(xp::User *user)
	{
		log("add online user to room");
		if (user)
		{
			if (auto room = get_room(default_roomid); room)
			{
				room->add_online_user(user->id, user);
			}
		}
		else
		{
			log("user ivalid", "error");
		}
	}
	xp::User *Server::try_login(xp::Connection *conn, xp::userid_type id, xp::userpassword_type password)
	{
		log(fmt::format("try login userid={}", id), "info");
		User *user = nullptr;
		{
			std::shared_lock lg{users_mtx_};
			if (auto iter = users_.find(id); iter != users_.end()) [[likely]]
			{
				log();
				user = iter->second.get();
				user->id = id;
				user->conn = conn;
			}
		}
		on_user_login(user);
		return user;
	}
	void Server::resume_room_users(xp::Room *room)
	{
		auto func = [](User *user) {
			log(fmt::format("user id={}", user->id), "!!!");
			
			auto id = user->id;
			auto handle = user->conn->task.handle;
			epoll_data_t data{.ptr = handle.address()};
			epoll_event epevent{.data = data};
			auto task = [user, id, epevent] {
				if (!user->has_msg())
					return;
				log(fmt::format("user {} to writev msg", id), "!!!");
				main_loop.event_handler_(epevent);
			};
			main_loop.add_task(task);
		};

		auto num = room->online_users_do(func);
		log(fmt::format("online users num={}", num), "info");
	}
	bool Server::handle_message(xp::MessageWrapper msg_wrapper, xp::userid_type from_userid)
	{
		log("handle message");
		auto msg = msg_wrapper.get();
		auto from_id = xp::ntoh(msg->from_id);
		auto to_id = xp::ntoh(msg->to_id);
		switch (xp::ntoh(msg->msg_type))
		{
		case xp::message_type::msg:
		case xp::message_type::logout:
		case xp::message_type::login_result:
		{
			auto context = std::string_view{msg->context, xp::ntoh(msg->context_size)};
			auto from_id = xp::ntoh(msg->from_id);
			auto to_id = xp::ntoh(msg->to_id);
			log(fmt::format("from {} to {} says : {}", from_id, to_id, context), "info");

			if (auto room = this->get_room(to_id); room) [[likely]]
			{
				log(fmt::format("add msg to room {}", room->id()), "info");
				room->add_msg(msg_wrapper, from_userid);
				resume_room_users(room);
			}
			return true;
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
			if (int fd = ::accept(lisent_fd, (sockaddr *)&addr, &len); fd >= 0)
			{
				xp::log(fmt::format("accept fd={}", fd));
				if (auto conn = std::make_unique<Connection>(fd, addr);
					conn->state) [[likely]]
				{
					log("conn is valid", "info");
					server->add_conn(fd, std::move(conn));
				}
				else
				{
					log("conn is invalid", "error");
				}
			}
			else
			{
				log("accept fd is invalid", "error");
				co_await std::suspend_always{};
			}
		}
		co_return;
	}

	xp::Room *Server::get_room(xp::roomid_type id)
	{
		std::shared_lock lg{rooms_mtx_};
		return rooms_[id].get();
	}
}

#endif