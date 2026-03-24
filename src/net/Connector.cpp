#include "net/Connector.h"
#include "Log/Logger.h"
#include <sys/socket.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <netinet/in.h>
#include <arpa/inet.h>


// 创建非阻塞 Socket
static int CreateNonblocking()
{
    int sockfd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, IPPROTO_TCP);
    if (sockfd < 0) 
    {
        LOG_FATAL << "socket create failed!";
    }
    return sockfd;
}

Connector::Connector(EventLoop* loop, const std::string& ip, uint16_t port)
    : loop_(loop), ip_(ip), port_(port), connect_(false), state_(kDisconnected)
{

}

Connector::~Connector()
{
}

void Connector::Start() 
{
    connect_ = true;
    loop_->RunInLoop(std::bind(&Connector::StartInLoop, this));
}

void Connector::Stop()
{
    // 1. 立刻把原子标志位设为 false，彻底切断重连的念想
    connect_ = false;

    // 2. 把真正的清理工作，扔给 EventLoop 所在的线程去安全执行
    // 必须用 QueueInLoop，防止在多线程环境下直接操作底层 Channel 导致崩溃
    loop_->QueueInLoop(std::bind(&Connector::StopInLoop, this));
}

void Connector::StartInLoop()
{
    loop_->AssertInLoopThread();
    if (connect_) 
    {
        Connect();
    }
}

void Connector::StopInLoop()
{
    loop_->AssertInLoopThread();

    // 只有在 "正在尝试连接中 (kConnecting)" 的状态下，才需要我们去擦屁股。
    // 如果是 kConnected，说明 fd 已经移交给 TcpConnection 了，归别人管了。
    // 如果是 kDisconnected，说明本来就没连，什么都不用做。
    if (state_ == kConnecting)
    {
        SetState(kDisconnected);

        // 1. 把插在 epoll 上的监听通道拔下来
        int sockfd = RemoveAndResetChannel();

        // 2. 神级复用：借用 Retry 函数来关掉底层的 Socket！
        // 仔细看你的 Retry 函数，它第一行就是 ::close(sockfd);
        // 然后它会判断 if (connect_) 才去重连。
        // 因为我们在 Stop() 里已经把 connect_ 设置为 false 了，
        // 所以 Retry 在这里只会乖乖地关掉 fd，绝对不会触发下一次重试！
        Retry(sockfd);
    }
}

void Connector::Connect() 
{
    int sockfd = CreateNonblocking();
    
    // 发起非阻塞的 connect
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port_);
    server_addr.sin_addr.s_addr = inet_addr(ip_.c_str());

    int ret = ::connect(sockfd, (struct sockaddr*)&server_addr, sizeof(sockaddr_in));
    int savedErrno = (ret == 0) ? 0 : errno;

    switch (savedErrno) 
    {
    case 0:
    case EINPROGRESS: // 重点：异步连接正在进行中！交给 epoll 去盯梢！
    case EINTR:
    case EISCONN:
        Connecting(sockfd);
        break;
    case EAGAIN:
    case EADDRINUSE:
    case EADDRNOTAVAIL:
    case ECONNREFUSED:
    case ENETUNREACH:
        Retry(sockfd);
        break;
    default:
        LOG_ERROR << "Unexpected error in connect() " << savedErrno;
        ::close(sockfd);
        break;
    }
}

void Connector::Connecting(int sockfd) 
{
    SetState(kConnecting);
    // 创建一个 Channel，挂在当前的 EventLoop 上
    channel_.reset(new Channel(loop_, sockfd));

    // 当 socket 变得可写时，说明连接建立成功（或者报错了）
    channel_->SetWriteCallback(std::bind(&Connector::HandleWrite, this));
    channel_->SetErrorCallback(std::bind(&Connector::HandleError, this));

    // 告诉 epoll：帮我盯着这个 fd 的可写事件 (EPOLLOUT)
    channel_->EnableWriting();
}

// 三次握手结束后，就会触发写事件
void Connector::HandleWrite() 
{
    LOG_INFO << "Connector::handleWrite " << state_;
    if (state_ == kConnecting) 
    {
        int sockfd = RemoveAndResetChannel();                                               // 拔出插头，这个 fd 以后要交给 TcpConnection 了

        // 检查是不是真的连上了（获取底层错误码）
        int err = 0;
        socklen_t optlen = sizeof(err);
        if (::getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &err, &optlen) < 0) {
            err = errno;
        }

        if (err) 
        {
            LOG_ERROR << "Connector::handleWrite - SO_ERROR = " << err;
            Retry(sockfd);
        }
        else
        {
            SetState(kConnected);
            if (connect_)
            {
                // 连接成功！通知大当家 TcpClient 去创建 TcpConnection！
                if(new_connection_callback_)
                    new_connection_callback_(sockfd);
            }
            else 
            {
                ::close(sockfd);
            }
        }
    }
}

void Connector::HandleError() 
{
    LOG_ERROR << "Connector::handleError";
    if (state_ == kConnecting)
    {
        int sockfd = RemoveAndResetChannel();
        Retry(sockfd);
    }
}

void Connector::Retry(int sockfd)
{
    ::close(sockfd);
    SetState(kDisconnected);
    if (connect_)
    {
        LOG_INFO << "Connector::retry - connecting to " << ip_ << " " << port_ << " in 2 seconds...";
        // 此处为了简化工程，休眠一会儿然后投递重试任务，实际可配合 TimerQueue
        loop_->RunAfter(2.0, std::bind(&Connector::StartInLoop, shared_from_this()));
    }
}

int Connector::RemoveAndResetChannel() 
{
    channel_->DisableAll();
    channel_->Remove();
    int sockfd = channel_->fd();
    
    // 把任务推迟到下一个事件循环销毁 channel，防止死锁
    loop_->QueueInLoop(std::bind(&Connector::ResetChannel, this));
    return sockfd;
}

void Connector::ResetChannel()
{
    channel_.reset();
}