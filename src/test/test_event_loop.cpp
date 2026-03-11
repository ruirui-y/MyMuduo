#include "net/EventLoop.h"
#include "Log/Logger.h"
#include "net/TcpConnection.h"
#include <iostream>
#include <thread>

// 定义一个简单的测试函数
void TestEventLoop() 
{
    LOG_INFO << "Starting EventLoop Test...";

    EventLoop loop;

    // 我们可以在 Loop 启动前，开启一个线程，模拟一下“外部唤醒”或者“任务投递”
    // 虽然我们还没写 Wakeup 逻辑，但基本的 Loop 已经能跑了
    std::thread t([&loop]() {
        std::this_thread::sleep_for(std::chrono::seconds(3));
        LOG_INFO << "3 seconds passed, stopping EventLoop...";
        loop.Quit(); // 测试 Quit 逻辑
        });
    t.detach();

    LOG_INFO << "Loop is running...";
    loop.Loop();
    LOG_INFO << "EventLoop stopped successfully.";
}

int main() {
    // 初始化一下你的日志（假设你在 Logger.h 里有个 Init 函数）
    // Logger::Init("test.log"); 

    TestEventLoop();
    return 0;
}