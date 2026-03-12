#include "net/Socket.h"
#include "Log/Logger.h"
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <netinet/tcp.h>

Socket::~Socket() 
{
    ::close(socket_fd_);
}

void Socket::BindAddress(const std::string& ip, uint16_t port)
{
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);

    if (::bind(socket_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0)
    {
        LOG_FATAL << "Socket::bind error";
    }
}

void Socket::Listen()
{
    if (::listen(socket_fd_, SOMAXCONN) < 0)
    {
        LOG_FATAL << "Socket::listen error";
    }
}

int Socket::Accept() 
{
    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);
    // accept4 可以直接设置 SOCK_NONBLOCK 和 SOCK_CLOEXEC，极其好用
    int connfd = ::accept4(socket_fd_, (struct sockaddr*)&addr, &len, SOCK_NONBLOCK | SOCK_CLOEXEC);

    if (connfd < 0) 
    {
        int err = errno;
        // 如果是因为信号中断，或者描述符不够了，这属于可恢复的错误
        if (err != EINTR && err != EMFILE)
        {
            LOG_ERROR << "Socket::accept error";
        }
    }
    
    return connfd;
}

void Socket::ShutdownWrite()
{
    if (::shutdown(socket_fd_, SHUT_WR) < 0)
    {
        LOG_ERROR << "sockets::shutdownWrite error";
    }
}

int Socket::GetTcpInfoError() const
{
    int optval;
    socklen_t optlen = sizeof(optval);
    if (::getsockopt(socket_fd_, SOL_SOCKET, SO_ERROR, &optval, &optlen) < 0)
    {
        return errno;
    }
    return optval;
}

void Socket::SetTcpNoDelay()
{
    int optval = 1;
    if (::setsockopt(socket_fd_, IPPROTO_TCP, TCP_NODELAY, &optval, sizeof(optval)) < 0)
    {
        LOG_ERROR << "sockets::setTcpNoDelay error";
    }
}
