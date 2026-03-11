#include "net/Acceptor.h"
#include "Log/Logger.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

Acceptor::Acceptor(EventLoop* loop, const std::string& ip, uint16_t port)
	:loop_(loop),
	accept_socket_(::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0)),
	accept_channel_(loop, accept_socket_.fd()),
	listening_(false)
{
    accept_socket_.BindAddress(ip, port);
	accept_channel_.SetReadCallback(bind(&Acceptor::HandleRead, this));
}

Acceptor::~Acceptor()
{

}

void Acceptor::Listen()
{
	listening_ = true;
	accept_socket_.Listen();
	accept_channel_.EnableReading();																	// 把 listen fd 加入 epoll
}

void Acceptor::HandleRead() 
{
    int connfd = accept_socket_.Accept(); // accept 新连接
    if (connfd >= 0) 
    {
        if (new_connection_callback_) 
        {
            // 将新 fd 抛给上层业务
            new_connection_callback_(connfd, "peer_address");
        }
        else
        {
            ::close(connfd);
        }
    }
    else
    {
        LOG_ERROR << "Acceptor::HandleRead error";
    }
}