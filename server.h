#ifndef SERVER_H_
#define SERVER_H_

#include "event_manager.h"
#include "net.h"
#include "co_net.h"
#include "co.hpp"

namespace xp
{
    class Server;
}
extern xp::Server *server;
extern xp::EventLoopManager loops;

namespace xp
{
    struct Connection;
    struct Room;
    struct User;
    struct Message;
    struct Connection;

    enum
    {
        userpassword_size = 16
    };

    using to_recv_size_type = uint32_t;
    //unique
    using messageid_type = uint32_t;
    //unique
    using userid_type = uint32_t;
    //unique
    using roomid_type = uint32_t;
    //unique once room
    using seqid_type = uint32_t;
    using context_type = std::string;
    using timestamp_type = std::time_t;
    using userpassword_type = char[userpassword_size];

    enum message_size_t
    {
        messageid_size = sizeof(messageid_type),

        to_recv_size_type_size = sizeof(to_recv_size_type),
        timestamp_size = sizeof(timestamp_type),
        userid_size = sizeof(userid_type),
        roomid_size = sizeof(roomid_type),
        seqid_size = sizeof(seqid_type),

        head_size = to_recv_size_type_size + timestamp_size + userid_size + roomid_size,
    };

    struct Message
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
    };

    //  /body size/ + /timestamp/ + /from id/ + /to id/ + /context/

    struct Room
    {
        xp::roomid_type id;
        char name[32];
        std::map<xp::messageid_type, xp::Message> messages;
        std::atomic<xp::seqid_type> current_seq;
        std::vector<xp::userid_type> users;
        xp::seqid_type get_seqid()
        {
            return current_seq++;
        }
    };

    struct User
    {
        userid_type id;
        char name[32]{"test"};
        int fd;
        std::vector<xp::roomid_type> rooms{256};
        std::unordered_map<xp::roomid_type, seqid_type> last_messages{{256, 0}};
    };

    class Server
    {
    public:
        Server();
        ~Server();
        bool try_login(int fd, xp::userid_type id, xp::userpassword_type password);
        BasicTask<> co_accept(const int fd);
        void close_conn(int fd);
        xp::Room &get_room(xp::roomid_type id);

        xp::Acceptor acceptor_;
        xp::BasicTask<> accept_task_;
    private:
        std::map<int, Connection> connections_;
        std::unordered_map<xp::userid_type, xp::User> users_;
        std::unordered_map<xp::roomid_type, xp::Room> rooms_;
        std::shared_mutex conn_mtx_;
        std::shared_mutex users_mtx_;
        std::shared_mutex rooms_mtx_;
    };

    Message Message::parse_context(std::string_view context)
    {
        char *p = const_cast<char *>(context.data());
        xp::to_recv_size_type to_recv_size;
        std::copy_n(p, to_recv_size_type_size, (char *)&to_recv_size);
        p += to_recv_size_type_size;

        xp::timestamp_type timestamp;
        std::copy_n(p, timestamp_size, (char *)&timestamp);
        p += timestamp_size;

        xp::userid_type from_id;
        std::copy_n(p, userid_size, (char *)&from_id);
        p += userid_size;

        xp::roomid_type to_id;
        std::copy_n(p, roomid_size, (char *)&to_id);
        p += roomid_size;

        xp::seqid_type seq = server->get_room(to_id).get_seqid();

        return {timestamp, from_id, to_id, seq, {context.begin(),context.end()}};
    }

    using ConnTask = AutoDestroyTask<FinalSuspendPromise>;
    struct Connection
    {
        int fd;
        sockaddr_in addr;
        userid_type id = -1;
        xp::ConnTask task;
    };
    ConnTask co_connection(int fd, sockaddr_in addr)
    {
        xp::log();
        std::string login_buf(64, '\0');

        constexpr int login_num = head_size + userpassword_size;
        bool login_read_result = false;
        co_await co_read(fd, login_buf.data(), login_num, login_read_result);
        
        if (!login_read_result)
        {
            to_del_fd = fd;
            xp::log("conn co_return");
            co_return;
        }

        auto message = xp::Message::parse_context(std::string_view(login_buf.begin(), login_buf.begin() + login_num));

        enum to_id_type
        {
            login = 0,
            retrister,
            off,
        };

        if (message.to_id == to_id_type::login)
        {
            log();
            userpassword_type password;
            char *p = message.context.data() + head_size;
            std::copy(p, p + userpassword_size, (char *)&password);
            if (server->try_login(fd, message.from_id, password))
            {
                log("login success", "info");
            }
            else
            {
                log();
                to_del_fd = fd;
                xp::log("conn co_return");
                co_return;
            }
        }
        else
        {
            log();
            to_del_fd = fd;
            xp::log("conn co_return");
            co_return;
        }
        log();
        //co_await std::suspend_always{};
        xp::EventAwaiter awaiter{&accept_loop, fd, false};
        co_await awaiter;
        log();

        std::string write_buf(64, '\0');
        std::string read_buf(64, '\0');
        char *write_ptr = write_buf.data();
        char *read_ptr = read_buf.data();

        constexpr int defualt_num = 10;
        int write_num = defualt_num;
        int read_num = defualt_num;
        // 验证
        int result = 0;
        bool flag = true;

        while (flag)
        {
            {
                sleep(sleep_time);
                log();
                int events = th_epevent.events;

                if (events & EPOLLIN)
                {
                    log();
                    result = ::recv(fd, read_ptr, read_num, MSG_DONTWAIT);
                    log(fmt::format("recv result={}", result));
                    if (result == 0)
                    {
                        xp::log();
                        break;
                    }
                    else if (result < 0)
                    {
                        log();
                        std::cout << "errno\n";
                        if (errno == EINTR || errno == EAGAIN)
                        {
                            xp::log();
                        }
                        else
                        {
                            xp::log();
                            break;
                        }
                    }
                    else if (result == read_num)
                    {
                        xp::log();
                        log(fmt::format("{} says : {}", fd, std::string_view(read_buf.data(), defualt_num)));
                        read_num = defualt_num;
                        read_ptr = &read_buf[0];
                        copy(read_buf.begin(), read_buf.end(), write_buf.begin());

                        events |= EPOLLOUT;
                        log(fmt::format("events={}", events));
                    }
                    else
                    {
                        xp::log();
                        read_num -= result;
                        read_ptr += result;
                    }
                }

                if (events & EPOLLOUT)
                {
                    log();
                    result = ::send(fd, write_ptr, write_num, MSG_DONTWAIT);
                    log(fmt::format("send result={}", result));
                    if (result == 0)
                    {
                        xp::log();
                        break;
                    }
                    else if (result < 0)
                    {
                        log();
                        std::cout << "errno\n";
                        if (errno == EINTR || errno == EAGAIN)
                        {
                            xp::log();
                        }
                        else
                        {
                            xp::log();
                            break;
                        }
                    }
                    else if (result == write_num)
                    {
                        xp::log();
                        write_num = defualt_num;
                        write_ptr = write_buf.data();
                    }
                    else
                    {
                        xp::log();
                        write_num -= result;
                        write_ptr += result;
                    }
                }

                if (events & EPOLLERR)
                {
                    xp::log(fmt::format("events={}", events));
                    break;
                }

                if (events & EPOLLRDHUP)
                {
                    xp::log(fmt::format("events={}", events));
                    break;
                }
            }
            log();
            co_await std::suspend_always{};
        }
        to_del_fd = fd;
        xp::log("conn co_return");
        co_return;
    }

    Server::Server()
    {
        log();
        acceptor_.listen();
        accept_task_ = co_accept(acceptor_.fd);
        auto ptr = accept_task_.handle.address();
        auto accept_event = xp::make_epoll_event(epoll_data_t{.ptr = ptr});
        loops[0].ctl(EventLoop::add, acceptor_.fd, &accept_event);
        log();
    }
    Server::~Server()
    {
        log();
    }

    bool Server::try_login(int fd, xp::userid_type id, xp::userpassword_type password)
    {
        log();
        {
            std::unique_lock lg{users_mtx_};
            users_[id] = xp::User{
                .id = id,
                .fd = fd};
        }
        return true;
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
                {
                    void *ptr;
                    log();
                    {
                        std::unique_lock lg{conn_mtx_};
                        auto &&conn = Connection{fd, addr, 0, xp::co_connection(fd, addr)};
                        ptr = conn.task.handle.address();
                        connections_.insert({fd, std::move(conn)});
                    }
                    //auto conn_event = xp::make_epoll_event(epoll_data_t{.ptr = ptr});
                    //loops.select(fd).ctl(EventLoop::add, fd, &conn_event);
                }
            }
            else
            {
                co_await std::suspend_always{};
            }
        }
        co_return;
    }

    void Server::close_conn(int fd)
    {
        log();
        ::close(fd);
        {
            std::unique_lock lg{conn_mtx_};
            connections_.erase(fd);
        }
    }
    xp::Room &Server::get_room(xp::roomid_type id)
    {
        std::shared_lock lg{rooms_mtx_};
        return rooms_[id];
    }
}

#endif