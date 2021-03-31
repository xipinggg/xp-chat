#ifndef SERVER_H_
#define SERVER_H_

#include "event_manager.h"
#include "net.h"
#include "co_net.h"
#include "co.hpp"



namespace xp
{
    extern xp::EventLoopManager loops;
    class Server
    {
    public:
        Server()
        {
            log();
            acceptor_.listen();
            accept_task_ = xp::co_accept(acceptor_.fd,
                                             [this](int fd, sockaddr_in addr) { this->accept_handler(fd, addr); });
            auto ptr = accept_task_.handle.address();
            auto accept_event = xp::make_epoll_event(epoll_data_t{.ptr = ptr});
            loops[0].ctl(EventLoop::add, acceptor_.fd, &accept_event);
            log();
        }
        ~Server()
        {
            log();
        }
        void accept_handler(int fd, sockaddr_in addr)
        {
            xp::log(fmt::format("accept fd={}", fd));
            {
                void *ptr;
                log();
                {
                    std::lock_guard<std::mutex> lg{mtx_};
                    auto &&conn = Connection{fd, addr, xp::co_connection(fd, addr)};
                    ptr = conn.task.handle.address();
                    connections_.insert({fd, std::move(conn)});
                }
                auto conn_event = xp::make_epoll_event(epoll_data_t{.ptr = ptr});
                loops.select(fd).ctl(EventLoop::add, fd, &conn_event);
                log();
            }
        }

        void close_conn(int fd)
        {
            log();
            ::close(fd);
            std::lock_guard<std::mutex> lg{mtx_};
            connections_.erase(fd);
        }

    private:
        xp::Acceptor acceptor_;
        xp::BasicTask<> accept_task_;
        std::map<int, Connection> connections_;
        std::unordered_map<int, User> users_;
        std::mutex mtx_;
    };

}

#endif