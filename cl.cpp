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
using namespace std;

constexpr int sleep_time = 5;
constexpr char server_ip[] = "127.0.0.1";
constexpr int server_port = 8888;

enum
{
    userpassword_size = 16
};

using body_size_type = uint32_t;
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

    body_size_type_size = sizeof(body_size_type),
    timestamp_size = sizeof(timestamp_type),
    userid_size = sizeof(userid_type),
    roomid_size = sizeof(roomid_type),
    seqid_size = sizeof(seqid_type),

    head_size = body_size_type_size + timestamp_size + userid_size + roomid_size,
};

struct Message
{
    //xp::messageid_type id;
    timestamp_type timestamp;
    userid_type from_id;
    roomid_type to_id;
    seqid_type seq;
    context_type context;
};

//  /body size/ + /timestamp/ + /message type/ + /from id/ + /to id/ + /context/

int main()
{
    int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in addr;
    addr.sin_addr.s_addr = inet_addr(server_ip);
    addr.sin_family = AF_INET;
    addr.sin_port = htons(server_port);
    int res = connect(fd, (sockaddr *)(&addr), sizeof(addr));
    assert(res == 0);

    timestamp_type timestamp = time(0);
    userid_type from_id = 2388;
    roomid_type to_id = 0;
    userpassword_type password = "abcdefghijklmno";

    body_size_type body_size = timestamp_size + userid_size + roomid_size + userpassword_size;

    char *p;
   
    p = (char *)&body_size;
    res = write(fd, p, body_size_type_size);
    if (res != body_size_type_size)
        cout << "1 fail\n";

    p = (char *)&timestamp;
    res = write(fd, p, timestamp_size);
    if (res != timestamp_size)
        cout << "2 fail\n";

    p = (char *)&from_id;
    res = write(fd, p, userid_size);
    if (res != userid_size)
        cout << "3 fail\n";

    p = (char *)&to_id;
    res = write(fd, p, roomid_size);
    if (res != roomid_size)
        cout << "4 fail\n";

    p = (char *)&password;
    res = write(fd, p, userpassword_size);
    if (res != userpassword_size)
        cout << "5 fail\n";

    sleep(3);

    constexpr int size = 10;
    char sbuf[size] = "123456789";
    char rbuf[size] = "87654321\0";

    res = write(fd, sbuf, size);
    if (res != size)
        cout << "6 fail\n";

    res = read(fd, rbuf, size);
    if (res != size)
        cout << "7 fail\n";
    cout <<"recv : "<< rbuf << endl;

    close(fd);
    if (res >= 0)
        cout << "all success\n";
    else
        cout << "fail\n";
}