#include <iostream>
#include <thread>
#include <pthread.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <stdio.h>
#include "logger.h"
using namespace std;
using namespace xp;

xp::Logger logger;
bool flag = true;
void f()
{
    int i = 1000;
    while (i--)
    {
        logger.commit();
    }
    sleep(2);
    i = 1000;
    while (i--)
    {
        logger.commit();
    }
    sleep(2);
    i = 1000;
    while (i--)
    {
        logger.commit();
    }
    sleep(2);
    i = 1000;
    while (i--)
    {
        logger.commit();
    }

}
void l()
{
    while(flag)
    {
        logger.output();
        sleep(3);
    }
}
int main()
{
    /*jthread j{f};
    jthread j2{l};

    sleep(10);
    flag = false;*/
    int i = 10000;
    while (i--)
    {
        logger.commit();
    }
    auto start = clock();
    logger.output();
    auto end = clock();
    cout << end << endl;
    cout<<start << endl;
    cout << (double)(end - start) / CLOCKS_PER_SEC << "s"<<endl;
    cout << "end\n";
}