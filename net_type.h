#ifndef XP_NET_TYPE_H
#define XP_NET_TYPE_H

#include <vector>
#include <atomic>
#include <map>
#include <unordered_map>
#include <bit>
#include <memory>
#include "thread_tools.h"
#include <fmt/format.h>
namespace xp
{
	inline constexpr auto hton(auto m) noexcept
	{
		if constexpr (std::endian::native == std::endian::little)
		{
			char *first = (char *)&m;
			char *last = first + sizeof(m);
			decltype(m) result{};
			std::reverse_copy(first, last, (char *)&result);
			return result;
		}
		return m;
	}

	inline constexpr auto ntoh(auto m) noexcept
	{
		if constexpr (std::endian::native == std::endian::little)
		{
			char *first = (char *)&m;
			char *last = first + sizeof(m);
			decltype(m) result{};
			std::reverse_copy(first, last, (char *)&result);
			return result;
		}
		return m;
	}

	struct Connection;
	struct Room;
	struct User;
	struct Message;
	struct Connection;

	enum
	{
		userpassword_size = 16
	};
	using code_type = uint32_t;
	using to_recv_size_type = uint32_t;
	//unique
	using messageid_type = uint32_t;
	//unique
	using userid_type = uint32_t;
	//unique
	using roomid_type = uint32_t;
	//unique once room
	using seqid_type = uint32_t;
	//using context_type = std::string;
	using timestamp_type = std::time_t;
	using userpassword_type = char[userpassword_size];
	using context_size_type = uint32_t;
	//using message_type = uint32_t;
	enum message_type : uint32_t
	{
		login = 0,
		retrister,
		logout,
		login_result,
		retrister_result,
		msg,
		online_users,
		outline_users,
		error,
		parse_error,
	};

	/*constexpr auto messageid_size = sizeof(messageid_type);
	constexpr auto to_recv_size_type_size = sizeof(to_recv_size_type);*/

	enum message_size_t
	{
		message_type_size = sizeof(message_type),
		timestamp_size = sizeof(timestamp_type),
		userid_size = sizeof(userid_type),
		roomid_size = sizeof(roomid_type),
		context_size_type_size = sizeof(context_size_type),
		head_size = message_type_size + timestamp_size + userid_size + roomid_size + context_size_type_size,
	};

	// /timestamp/ + /from_id/ + /to_id/ + /context_size/ + /context/
	// /code type/ + /context_size/ + / context /
	struct Message
	{
		message_type msg_type;
		timestamp_type timestamp;
		userid_type from_id;
		roomid_type to_id;
		context_size_type context_size;
		char context[0];
	} __attribute__((packed));

	struct MessageWrapper
	{
		std::shared_ptr<Message> ptr = {nullptr};

		MessageWrapper(const context_size_type context_size)
			: ptr{static_cast<Message *>(::malloc(sizeof(Message) + context_size))}
		{
			ptr->context_size = xp::hton(context_size);
		}
		MessageWrapper(const MessageWrapper &) = default;
		MessageWrapper &operator=(const MessageWrapper &) = default;

		MessageWrapper(MessageWrapper &&) noexcept = default;
		MessageWrapper &operator=(MessageWrapper &&) noexcept = default;
		~MessageWrapper() = default;

		Message *get() const noexcept
		{
			return ptr.get();
		}
		char *data() const noexcept
		{
			return (char *)ptr.get();
		}
		char *context_data() const noexcept
		{
			return (char *)ptr.get() + xp::head_size;
		}

		size_t size() const noexcept
		{
			return xp::head_size + context_size();
		}
		context_size_type context_size() const noexcept
		{
			auto size = ptr->context_size;
			return xp::ntoh(size);
		}
	};

	class Room
	{
		xp::roomid_type id_;

		std::map<xp::messageid_type, xp::Message> messages_;
		std::unordered_map<xp::userid_type, xp::User *> users_;
		std::unordered_map<xp::userid_type, xp::User *> online_users_;
		xp::SwapBuffer<std::pair<xp::userid_type, xp::User *>> online_users_buf_;

		std::shared_mutex users_mtx_;
		std::shared_mutex online_users_mtx_;
		xp::SpinLock msg_lock_;
		//std::atomic<int> last_msg_id_;

	public:
		Room(xp::roomid_type id) : id_{id} {}
		auto id() { return id_; }
		auto last_msg_index()
		{
			std::lock_guard lg{msg_lock_};
			return messages_.size() - 1;
		}
		void add_msg(xp::MessageWrapper msg, userid_type not_add_id = 0);
		auto get_msg(int index)
		{
			std::lock_guard lg{msg_lock_};
			return messages_[index];
		}
		void add_user(xp::userid_type id, xp::User *user)
		{
			std::unique_lock ul{users_mtx_};
			users_.insert(std::make_pair(id, user));
		}
		void del_user(userid_type id)
		{
			std::unique_lock ul{users_mtx_};
			if (auto iter = users_.find(id); iter != users_.end())
				users_.erase(iter);
		}
		auto online_users_do(std::function<void(User *)> func)
		{
			update_online_users();
			std::shared_lock sl{online_users_mtx_};
			for (auto u : online_users_)
			{
				func(u.second);
			}
			return online_users_.size();
		}
		void add_online_user(xp::userid_type id, xp::User *user)
		{
			online_users_buf_.add({id, user});
		}
		void del_outline_user(userid_type id)
		{
			update_online_users();
			std::unique_lock{online_users_mtx_};
			if (auto iter = online_users_.find(id); iter != online_users_.end())
				online_users_.erase(iter);
		}
		void update_online_users();
	};

	struct User
	{
		xp::userid_type id;
		std::string name;
		bool state;
		xp::Connection *conn;
		std::vector<xp::roomid_type> rooms;
		xp::SwapBuffer<xp::MessageWrapper> messages;
		//
		User(xp::userid_type i, std::string n, xp::Connection *cn = nullptr)
			: id{i}, name{n}, state{false}, conn{cn}, rooms{10086} {}
		bool has_msg()
		{
			return !messages.empty();
		}
		void add_msg(xp::MessageWrapper msg)
		{
			messages.add(std::move(msg));
		}
		auto get_msg()
		{
			return messages.get();
		}
	};

	void xp::Room::update_online_users()
	{
		if (online_users_buf_.empty())
			return;
		auto add_waiters = online_users_buf_.get();
		std::unique_lock{online_users_mtx_};
		for (auto user : add_waiters)
		{
			online_users_.insert(std::make_pair(user.first, user.second));
		}
	}
	void xp::Room::add_msg(xp::MessageWrapper msg, userid_type not_add_id)
	{
		if (auto p = (Message *)msg.data(); 
			xp::ntoh(p->msg_type) == message_type::msg)
		{
			log("","test");
			std::shared_lock{users_mtx_};
			for (auto &userdata : users_)
			{
				auto user = userdata.second;
				if (user && not_add_id != user->id) [[likely]]
				{
					user->add_msg(msg);
				}
			}
		}
		else
		{
			log("","test");
			update_online_users();
			std::shared_lock{online_users_mtx_};
			for (auto &userdata : online_users_)
			{
				auto online_user = userdata.second;
				log(fmt::format("online_user {}", online_user->id), "test");
				if (online_user && not_add_id != online_user->id) [[likely]]
				{
					log(fmt::format("add to online_user {}", online_user->id), "test");
					online_user->add_msg(msg);
				}
			}
		}
	}

	inline std::tuple<char *, size_t> parse_recv_buf(
		std::vector<xp::MessageWrapper> &msgs, char *buf, size_t num)
	{
		while (num >= head_size)
		{
			auto p = (xp::Message *)buf;
			auto context_size = xp::ntoh(p->context_size);
			auto msg = xp::MessageWrapper{context_size};
			if (const auto msg_size = head_size + context_size; num >= msg_size)
			{
				std::copy_n(buf, msg_size, msg.data());
				buf += msg_size;
				num -= msg_size;
				msgs.emplace_back(std::move(msg));
			}
			else
			{
				break;
			}
		}
		return std::tuple{buf, num};
	}
	inline xp::MessageWrapper make_message(message_type msg_type, userid_type from_id,
										   roomid_type to_id, std::string &context)
	{
		xp::MessageWrapper msg_wp{context.size()};
		auto msg = msg_wp.get();

		msg->msg_type = xp::hton(msg_type);
		msg->timestamp = xp::hton(std::time(0));
		msg->from_id = xp::hton(from_id);
		msg->to_id = xp::hton(to_id);

		xp::context_size_type cs{context.size()};
		msg->context_size = xp::hton(cs);

		std::copy_n(context.data(), context.size(), msg_wp.context_data());

		return msg_wp;
	}
}

#endif
