#include "net/EventLoop.h"
#include <iostream>

void printTime() 
{
    std::cout << "Timer expired at " << Timestamp::now().toString() << std::endl;
}

int main()
{
    EventLoop loop;

    // 测试 1：3秒后执行一次
    loop.RunAfter(3.0, printTime);

    // 测试 2：每1秒执行一次（循环定时器）
    loop.RunEvery(1.0, []()
        {
            std::cout << "Repeated timer fired!" << std::endl;
        });

    loop.Loop();
    return 0;
}