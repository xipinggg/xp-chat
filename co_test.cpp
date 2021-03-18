#include <unistd.h>
#include <sys/poll.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <error.h>
#include <cassert>
#include <cstring>
#include <thread>
#include <vector>
#include <tuple>
#include <map>
#include "co.hpp"
#include "co_acceptor.h"
#include "co_epoller.h"
using namespace std;

int sleep_time = 1;
//std::map<int, Task> coros;

//thread_local uint32_t cur_co_revents;
Epoller epoller;

int main()
{
    trace();

    auto res = Singleton<TasksManager>::create();
    auto &tasks_manager = *Singleton<TasksManager>::get();

    int listen_fd = -1;
    auto revent = make_shared<uint32_t>();
    auto co_ls_ac = co_listen_accept(&listen_fd, revent);

    auto &lktasks = tasks_manager[listen_fd];
    auto &tasks = lktasks.tasks;
    auto &shmtx = lktasks.shmtx;

    auto co_iter = tasks.insert(tasks.begin(), 
                        std::make_pair(listen_fd, std::move(co_ls_ac)));
    epoll_event ls_event = Epoller::make_event(&(*co_iter));
    epoller.ctl(EPOLL_CTL_ADD, listen_fd, &ls_event);

    while (true)
    {
        sleep(sleep_time);
        int num = 0;
        auto events = epoller.epoll(num);
        cout << "epoll num " << events.size() << endl;
        for (auto event : events)
        {
            sleep(sleep_time);

            *revent = event.events;
            auto ptr = static_cast<Epoller::Event_data_ptr>(event.data.ptr);

            const auto fd = ptr->first;
            auto &co = ptr->second;
            std::cout << "resume fd " << fd << endl;
            co.resume();
            if (co.done())
            {
                epoller.ctl(EPOLL_CTL_DEL, fd, nullptr);
                std::unique_lock<shared_mutex> unilkgd(shmtx);
                tasks.erase(fd);
                //co.destory();
            }
        }
    }
}
