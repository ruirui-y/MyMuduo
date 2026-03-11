#include "net/EventLoop.h"
#include "net/TcpServer.h"
#include "Log/Logger.h"
#include <iostream>

int main(int argc, char* argv[])
{
    // 1. 设置日志级别（可选，根据你的 Log 实现决定）
    // Logger::SetLogLevel(Logger::INFO);

    // 2. 创建主事件循环 EventLoop (这是心脏)
    EventLoop loop;

    // 3. 创建 TcpServer 对象
    // 监听本地 8080 端口，你可以换成你喜欢的端口
    uint16_t port = 8080;
    TcpServer server(&loop, "127.0.0.1", port);

    // 注册消息回调
    server.SetMessageCallback([](const std::shared_ptr<TcpConnection>& conn, Buffer* buf)
        {
            std::string data = buf->RetrieveAllAsString();
            LOG_INFO << "Received data: " << data;
            conn->Send(data);
        });

    // 4. 启动服务器监听
    server.Start(3);

    // 5. 开启事件循环
    LOG_INFO << "Server is running on port " << port << " ...";
    loop.Loop();

    return 0;
}