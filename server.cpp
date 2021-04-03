#include "server.h"

using namespace std;
using namespace xp;

namespace xp
{
    Server::Server()
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
    void Server::accept_handler(int fd, sockaddr_in addr)
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
            auto conn_event = xp::make_epoll_event(epoll_data_t{.ptr = ptr});
            loops.select(fd).ctl(EventLoop::add, fd, &conn_event);
            log();
        }
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

} // namespace xp
