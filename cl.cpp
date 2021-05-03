#include "cl.h"
#include <thread>
using namespace std;
using namespace xp;

int main()
{
    int fd = -1;

    userid_type userid;
    cout << "请输入账号:\n";
    cin >> userid;
    cout << "请输入密码:\npasspass\n";
    MessageWrapper msg_wp{userpassword_size};
    auto msg = msg_wp.get();
    {
        msg->msg_type = xp::hton(login);
        msg->timestamp = xp::hton(std::time(0));
        msg->from_id = xp::hton(userid);
        msg->to_id = xp::hton(0);
        msg->context_size = xp::hton(userpassword_size);
        fd = get_fd();
        auto res = write(fd, msg_wp.data(), msg_wp.size());

        if (res != head_size + userpassword_size)
            cout << "login fail\n";
    }
    cout << "---------start---------\n";
    jthread jr{[fd] {
    while (fd >= 0)
    {
        auto msg = recv_msg(fd);
        output(msg);
    } }};

    cin.clear();
    string msgctx;
    //cin >> msg;
    getline(cin, msgctx);
    while (1)
    {
        cin.clear();
        //cin >> msg;
        getline(cin, msgctx);
        int res = send_msg(fd, message_type::msg, userid, default_roomid, msgctx);
        cout << "##send num : " << res << endl;
    }

    close(fd);

    cout << "----------end----------\n";
}