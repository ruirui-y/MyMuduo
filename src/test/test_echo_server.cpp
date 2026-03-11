#include <iostream>
#include <string>
#include "Log/Logger.h"
#include "net/TcpServer.h"                                      
#include "net/TcpConnection.h"

class EchoServer 
{
public:
    EchoServer(EventLoop* loop, const std::string& ip, uint16_t port)
        : server_(loop, ip, port, 10) {
        server_.SetConnectionCallback(std::bind(&EchoServer::OnConnection, this, std::placeholders::_1));
        server_.SetMessageCallback(std::bind(&EchoServer::OnMessage, this, std::placeholders::_1, std::placeholders::_2));
    }

    void Start(int threadNum) {
        server_.Start(threadNum);
    }

private:
    void OnConnection(const std::shared_ptr<TcpConnection>& conn) 
    {
        // 使用 LOG_INFO 替代 std::cout
        if (conn->Connected()) 
        {
            LOG_INFO << "New connection [fd:" << conn->fd()
                << "] assigned to thread: " << std::this_thread::get_id();
        }
        else {
            LOG_INFO << "Connection closed [fd:" << conn->fd() << "]";
        }
    }

    void OnMessage(const std::shared_ptr<TcpConnection>& conn, Buffer* buf) {
        std::string msg = buf->RetrieveAllAsString();
        // 如果数据量巨大，不要把整个内容打印到日志里，否则会严重拖慢 IO 线程
        LOG_INFO << "Received " << msg.size() << " bytes from fd: " << conn->fd();
        conn->Send(msg);
    }

    TcpServer server_;
};

int main() {
    EventLoop loop;
    EchoServer server(&loop, "127.0.0.1", 8888);

    // 开启 3 个子线程处理连接
    server.Start(3);

    loop.Loop();
    return 0;
}