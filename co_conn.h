#ifndef CO_CONN_H
#define CO_CONN_H

#include <unistd.h>
#include <netinet/in.h>
#include <tuple>
#include <string>
#include <map>
#include <iostream>
#include "co_epoller.h"

#define trace() std::cout << __FUNCTION__ << " : " << __FILE__ << " : " << __LINE__ << std::endl


namespace
{
    inline bool is_read_event(const int event)
    {
        return event & (EPOLLIN | EPOLLPRI | EPOLLRDHUP);
    }
    inline bool is_write_event(const int event)
    {
        return event & EPOLLOUT;
    }
    inline bool is_close_event(const int event)
    {
        return (event & EPOLLHUP) && !(event & EPOLLIN);
    }
    inline bool is_error_event(const int event)
    {
        return event & (EPOLLERR | POLLNVAL);
    }
} // namespace

class Connection
{
public:
    Connection(std::tuple<int, sockaddr_in, int> conn_data)
    : Connection(get<0>(conn_data), get<1>(conn_data), get<2>(conn_data))
    {
        //trace();
    }
    Connection(int fd, sockaddr_in addr, int events)
        : fd_(fd), addr_(addr), events_(events), buf_(1024,'\0'), close_(false)
    {
        trace();
    }
    Connection(const Connection &) = delete;
    Connection &operator=(const Connection &) = delete;
    Connection(Connection &&) = delete;
    Connection &operator=(Connection &&) = delete;
    ~Connection()
    {
        trace();
        ::close(fd_);
    }
    bool handle_event(const int event)
    {
        trace();
        if (is_read_event(event))
        {
            handle_read();
        }
        if (is_write_event(event))
        {
            handle_write();
        }
        if (is_close_event(event))
        {
            handle_close();
        }
        if (is_error_event(event))
        {
            handle_error();
        }
        return close_;
    }
    void handle_read()
    {
        auto n = buf_.capacity();
        auto buf_start = buf_.data();
        constexpr int flags = 0;

        ssize_t res = ::recv(fd_, buf_start, n, flags);
        if (res <= 0)
        {
            close_ = true;
            return;
        }

        std::cout << "recv res " << res <<std:: endl;
        buf_[res] = '\0';
        std::cout << fd_ << " : ";
        printf("%s\n", buf_start);
    }

    void handle_write()
    {

    }
    void handle_close()
    {
        close_ = true;
    }
    void handle_error()
    {

    }
private:
    //non block fd
    int fd_;
    sockaddr_in addr_;
    int events_;
    std::string buf_;
    bool close_;
};

Task co_conn(std::tuple<int, sockaddr_in, uint32_t> conn_data, 
                    std::shared_ptr<uint32_t> revent)
{
    trace();

    extern int sleep_time;
    Connection conn(conn_data);
    co_await std::suspend_always{};
    while (true)
    {
        sleep(sleep_time);
        if (auto is_close = conn.handle_event(*revent);
            is_close)
        {
            co_return;
        }
        co_await std::suspend_always{};
    }
    co_return;
}


#endif // !CO_CONN_H