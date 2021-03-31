#include <concepts>
#include <bits/stdc++.h>

#include "logger.h"
#include "event_manager.h"
#include "thread_tools.h"
#include <fmt/chrono.h>
using namespace std;
using namespace xp;

//xp::EventLoop log_loop{};

template <>
xp::Logger<> xp::Singleton<xp::Logger<>>::instance_{};
static auto &logger = xp::Singleton<xp::Logger<>>::get();

void g()
{
    logger.commit("gggg");
}

void f()
{
    logger.commit("ffff");
    g();
}

bool k = true;
int main()
{

    jthread j{[]{while (k)
    {
        logger.output();
        sleep(1);
    }
    }};
    cout << "hhh\n";
    logger.commit("mmmm");
    f();
    logger.output();
    log();
    cout << "hhh\n";
    sleep(5);
    k = false;
}
