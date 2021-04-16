#ifndef XP_NET_TYPE_H
#define XP_NET_TYPE_H

#include <vector>
#include <atomic>
#include <map>
#include <unordered_map>
#include <bit>
#include <memory>
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
		MessageWrapper&operator=(const MessageWrapper &) = default;

		MessageWrapper(MessageWrapper &&) noexcept = default;
		MessageWrapper&operator=(MessageWrapper &&) noexcept = default;
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

	struct Room
	{
		xp::roomid_type id;
		//char name[32];
		Room(xp::roomid_type i) : id{i} {}
		std::map<xp::messageid_type, xp::Message> messages;
		std::atomic<xp::seqid_type> current_seq;
		std::unordered_map<xp::userid_type, xp::User *> users;
		xp::seqid_type get_seqid()
		{
			return current_seq++;
		}
	};

	struct User
	{
		xp::userid_type id;
		std::string name;
		bool state;
		xp::Connection *conn;
		//non
		std::vector<xp::roomid_type> rooms;
		User(xp::userid_type i, std::string n)
			: id{i}, name{n}, state{false}, conn{nullptr}, rooms{}
		{
		}
		User(const User &) = delete;
		User &operator=(const User &) = delete;
		User(User &&u) noexcept
			: id{u.id}, name{std::move(u.name)},
			  state{u.state}, conn{u.conn}, rooms{std::move(u.rooms)}
		{
		}
		User &operator=(User &&u) noexcept
		{
			id = u.id;
			name = std::move(u.name);
			state = u.state;
			conn = u.conn;
			rooms = std::move(u.rooms);
			return *this;
		}
	};

	xp::MessageWrapper &&make_msg(int fd, message_type msg_type,
			userid_type from_id, roomid_type to_id, std::string &context)
	{
		MessageWrapper msg_wp{context.size()};
		auto msg = msg_wp.get();

		msg->msg_type = xp::hton(msg_type);
		msg->timestamp = xp::hton(std::time(0));
		msg->from_id = xp::hton(from_id);
		msg->to_id = xp::hton(to_id);

		xp::context_size_type cs{context.size()};
		msg->context_size = xp::hton(cs);

		std::copy_n(context.data(), context.size(), msg_wp.context_data());
		
		return std::move(msg_wp);
	}
} // namespace xp

/*struct Message
	{
		//xp::messageid_type id;
		xp::timestamp_type timestamp;
		xp::userid_type from_id;
		xp::roomid_type to_id;
		xp::seqid_type seq;
		xp::context_type context;
		std::string_view context_view() const noexcept
		{
			return {context.begin() + head_size, context.end()};
		}
		static Message parse_context(std::string_view context);
		static void make_context_head(Message &message)
		{
			auto &context = message.context;
			to_recv_size_type to_recv_size = context.size() - to_recv_size_type_size;

			auto index = 0;
			std::copy_n((char *)&to_recv_size, to_recv_size_type_size, context.data() + index);
			
			index = to_recv_size_type_size;
			std::copy_n((char *)&message.timestamp, timestamp_size, context.data() + index);
		
			index = userid_size;
			std::copy_n((char *)&message.from_id, userid_size, context.data() + index);

			index = roomid_size;
			std::copy_n((char *)&message.to_id, roomid_size, context.data() + index);
		}
	};*/

#endif
