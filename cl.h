#include <unistd.h>
#include <sys/poll.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <cassert>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <string_view>
#include <iostream>
#include <ctime>
#include "net_type.h"
#include <fmt/format.h>
using namespace std;
using namespace xp;
constexpr int sleep_time = 0;
constexpr char server_ip[] = "127.0.0.1";
constexpr int server_port = 8888;

int success_id = 0;
int &fail_id = success_id;

enum
{
    default_roomid = 10086,
};

auto make_str_time(const std::time_t t = std::time(nullptr)) noexcept
{
    constexpr size_t maxsize = 20;
    std::string str(maxsize, '\0');
    const char *format = "%F %T";
    std::strftime(str.data(), maxsize, format, std::localtime(&t));
    return str;
}

void test(int res)
{
    if (res > 0)
        ++success_id;
    else
        cout << ++fail_id << " fail\n";
}

int get_fd()
{
    int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    sockaddr_in addr;
    addr.sin_addr.s_addr = inet_addr(server_ip);
    addr.sin_family = AF_INET;
    addr.sin_port = htons(server_port);
    int res = connect(fd, (sockaddr *)(&addr), sizeof(addr));
    assert(res == 0);
    return fd;
}

int send_msg(int fd, message_type msg_type,
              userid_type from_id, roomid_type to_id, string &context)
{
    MessageWrapper msg_wp{context.size()};
    auto msg = msg_wp.get();

    msg->msg_type = xp::hton(msg_type);
    msg->timestamp = xp::hton(std::time(0));
    msg->from_id = xp::hton(from_id);
    msg->to_id = xp::hton(to_id);

    xp::context_size_type  cs{context.size()};
    msg->context_size = xp::hton(cs);

    copy_n(context.data(), context.size(), msg_wp.context_data());

    auto res = write(fd, msg_wp.data(), msg_wp.size());
    
    //res = write(fd, context.data(), context.size());

    if (res != head_size + context.size())
        cout <<res << "send msg fail\n";
    return res;
}

auto recv_msg(int fd)
{
    MessageWrapper msg_head{0};
    auto res = read(fd, msg_head.data(), head_size);
    if (res < 0)
    {
        cout << res<<"read msg head fail\n";
    }

    auto msg = msg_head.get();
    auto context_size = xp::ntoh(msg->context_size);
    string context(context_size, '\0');
    assert(context_size == context.size());
    res = read(fd, context.data(), context_size);
    if (res < 0)
        cout << "read msg context fail\n";

    return tuple{msg_head, context};
}

void output(decltype(recv_msg(1)) &msg_tuple)
{
    auto msg_head = get<0>(msg_tuple);
    auto context = get<1>(msg_tuple);
    auto msg = msg_head.get();
    auto timestamp = xp::ntoh(msg->timestamp);
    auto str_time = make_str_time(timestamp);

    auto from_id = xp::ntoh(msg->from_id);

    auto str = fmt::format("{}  {}\n{}\n", str_time, from_id, context);
    cout << str;
}