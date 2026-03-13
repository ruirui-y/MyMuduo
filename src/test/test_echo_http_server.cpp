#include <iostream>
#include <string>
#include "Log/Logger.h"
#include "net/TcpServer.h"                                      
#include "net/TcpConnection.h"

class EchoServer
{
public:
    EchoServer(EventLoop* loop, const std::string& ip, uint16_t port)
        : server_(loop, ip, port, 10) 
    {
        server_.SetConnectionCallback(
            [this](const std::shared_ptr<TcpConnection>& conn)
            {
                OnConnection(conn);
            });

        server_.SetMessageCallback(
            [this](const std::shared_ptr<TcpConnection>& conn, Buffer* buffer)
            {
                OnMessage(conn, buffer);
            });
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

    void OnMessage(const std::shared_ptr<TcpConnection>& conn, Buffer* buf) 
    {
        std::string msg = buf->RetrieveAllAsString();
        
        std::string response =
            "HTTP/1.1 200 OK\r\n"
            "Content-Length: 13\r\n"
            "Content-Type: text/plain\r\n"
            "\r\n"
            "Hello, Muduo!";

        conn->Send(response);
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