#include "cl.h"
#include <thread>
using namespace std;
using namespace xp;

int main()
{

    int fd = get_fd();

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
        auto res = write(fd, msg_wp.data(), msg_wp.size());

        //res = write(fd, context.data(), context.size());
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

    while (1)
    {
        string msg;
        cin >> msg;
        int res = send_msg(fd, message_type::msg, userid, default_roomid, msg);
        cout << "send : " << msg << "  res : " << res << endl;
    }

    close(fd);

    // string password(userpassword_size, '0');
    /*string str_password;
    while (str_password.size() == 0 || str_password.size() > userpassword_size)
    {
        cout << "请输入密码:\n";
        cin >> str_password;
    }
    copy_n(str_password.data(), str_password.size(), password.data());*/
    //send_msg(fd, message_type::login, userid, 0, password);
    /*cout << "---------start--------\n";
    jthread re{[fd] {
        while (1)
        {
            auto msg = recv_msg(fd);
            cout << "recv one msg\n";
            output(msg);
        }
    }};
    int write_res = 1;
    while (write_res >= 0)
    {
        string context;
        //getline(cin, context);
        cin >> context;
        write_res = send_msg(fd, message_type::msg,
                             userid, default_roomid, context);
    }
*/
    cout << "----------end----------\n";
}