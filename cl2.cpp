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
using namespace std;
using namespace xp;
constexpr int sleep_time = 5;
constexpr char server_ip[] = "127.0.0.1";
constexpr int server_port = 8888;

int success_id = 0;
int &fail_id = success_id;

void test(int res)
{
    if (res > 0)
        cout << ++success_id << " success\n";
    else
        cout << ++fail_id << " fail\n";
}

int main()
{

    int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    {
        sockaddr_in addr;
        addr.sin_addr.s_addr = inet_addr(server_ip);
        addr.sin_family = AF_INET;
        addr.sin_port = htons(server_port);
        int res = connect(fd, (sockaddr *)(&addr), sizeof(addr));
        assert(res == 0);
    }

    userid_type userid{2388};

    MessageWrapper login_message{userpassword_size};
    auto msg = login_message.get();
    msg->msg_type = xp::hton(message_type::login);
    msg->timestamp = xp::hton(std::time(0));
    msg->from_id = xp::hton(userid);
    msg->to_id = 0;
    msg->context_size = xp::hton(userpassword_size);

    sleep(sleep_time);
    auto res = write(fd, login_message.data(), login_message.size());
    cout << "send num : " << res << endl;
    test(res);
    sleep(sleep_time);

    constexpr char *ctx = "this is fxp!"; //"fxp is very handsome!";
    int ctx_size = strlen(ctx);
    MessageWrapper write_message{ctx_size};
    msg = write_message.get();
    msg->msg_type = xp::hton(message_type::msg);
    msg->timestamp = xp::hton(std::time(0));
    msg->from_id = xp::hton(userid);
    msg->to_id = xp::hton(xp::roomid_type{10086});
    msg->context_size = xp::hton(context_size_type{ctx_size});

    sleep(sleep_time);
    std::copy_n(ctx, ctx_size, write_message.context_data());
    res = write(fd, write_message.data(), write_message.size());
    cout << "send num : " << res << endl;
    test(res);

    sleep(sleep_time);
    res = read(fd, login_message.data(), head_size);
    cout << "recv num : " << res << endl;
    test(res);

    ctx_size = login_message.context_size();
    auto read_msg = MessageWrapper{ctx_size};

    cout << "from_id : \n"
         << xp::ntoh(login_message.get()->from_id) << endl;

    copy_n(login_message.data(), head_size, read_msg.data());

    res = read(fd, read_msg.context_data(), ctx_size);
    cout << "recv num : " << res << endl;
    test(res);
    if (xp::ntoh(read_msg.get()->msg_type) == message_type::msg)
    {
        cout << "recv : \n"
             << xp::ntoh(read_msg.get()->from_id) << " says : "
             << string{read_msg.context_data(), ctx_size} << endl;
    }
    //sleep(5);
    close(fd);
    cout << "end\n";
}