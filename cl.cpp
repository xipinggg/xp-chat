#include <unistd.h>
#include <sys/poll.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <cassert>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <iostream>

using namespace std;


constexpr int sleep_time = 5;
constexpr char server_ip[] = "127.0.0.1";
constexpr int server_port = 8888;

int main()
{
    int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in addr;
    addr.sin_addr.s_addr= inet_addr(server_ip);  
    addr.sin_family = AF_INET;
    addr.sin_port = htons(server_port);  
    int res = connect(fd, (sockaddr *)(&addr), sizeof(addr));
    assert(res == 0);
    char s[] = "abcdefghij";
    res = send(fd, s, strlen(s), 0);
    cout << "send res " << res << endl;
    sleep(sleep_time);
    cout << "close\n";
    close(fd);
}