#include "net/Acceptor.h"
#include "Log/Logger.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>

Acceptor::Acceptor(EventLoop* loop, const std::string& ip, uint16_t port)
	:loop_(loop),
	accept_socket_(::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0)),
	accept_channel_(loop, accept_socket_.fd()),
	listening_(false),
    idle_fd_(::open("/dev/null", O_RDONLY | O_CLOEXEC))
{
    accept_socket_.BindAddress(ip, port);
    accept_channel_.SetReadCallback([this]() { HandleRead(); });
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
        // 如果文件描述符耗尽
        if(errno == EMFILE)
        {
            LOG_ERROR << "Acceptor::HandleRead error: EMFILE, fd limit reached!";
            ::close(idle_fd_);                                                              // 1. 扔掉占坑 fd，腾出一个名额
            idle_fd_ = ::accept(accept_socket_.fd(), NULL, NULL);                           // 2. 接收这个累赘
            ::close(idle_fd_);                                                              // 3. 优雅地挂断它
            idle_fd_ = ::open("/dev/null", O_RDONLY | O_CLOEXEC);                           // 4. 重新占坑，以备下次使用
        }
        else
        {
            LOG_ERROR << "Acceptor::HandleRead error";
        }
    }
}