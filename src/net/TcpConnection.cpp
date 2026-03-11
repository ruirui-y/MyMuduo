#include "net/TcpConnection.h"
#include "Log/Logger.h"
#include "net/EventLoop.h"
#include <unistd.h>
#include <errno.h>

TcpConnection::TcpConnection(EventLoop* loop, int sockfd)
    : loop_(loop),
    socket_(new Socket(sockfd)),
    channel_(new Channel(loop, sockfd)),
    state_(kConnecting)
{
    channel_->SetWriteCallback(std::bind(&TcpConnection::HandleWrite, this));
    channel_->SetReadCallback(std::bind(&TcpConnection::HandleRead, this));
    channel_->SetCloseCallback(std::bind(&TcpConnection::HandleClose, this));
    channel_->SetErrorCallback(std::bind(&TcpConnection::HandleError, this));
}

TcpConnection::~TcpConnection()
{
    LOG_INFO << "TcpConnection::~TcpConnection fd=" << socket_->fd();
}

void TcpConnection::ConnectEstablished()
{
    // 1. 设置状态为已连接
    SetState(kConnected);

    // 2. 绑定 Channel 与 TcpConnection 的生命周期
    // 这样在 Channel::HandleEvent 执行期间，TcpConnection 不会被析构
    channel_->tie(shared_from_this());

    // 3. 注册读事件到 Poller
    channel_->EnableReading();

    // 4. 通知用户连接已经建立
    if (connection_callback_)
    {
        connection_callback_(shared_from_this());
    }
}

void TcpConnection::Send(const std::string& data)
{
    if (Connected())
    {
        // 同线程直接发送
        if (loop_->IsInLoopThread())
        {
            SendInLoop(data);
        }
        else
        {
            // conn与调用者不在同一个线程，将该任务投递到conn所在的线程中执行
            loop_->RunInLoop(std::bind(&TcpConnection::SendInLoop, this, data));
        }
    }
}

void TcpConnection::SendInLoop(const std::string& data)
{
    loop_->AssertInLoopThread();
    ssize_t nwrote = 0;

    // 如果之前没有数据再发，且当前的缓冲区为空，尝试直接发送
    if (!channel_->IsWriting() && output_buffer_.ReadableBytes() == 0)
    {
        nwrote = ::write(socket_->fd(), data.data(), data.size());
        if (nwrote >= 0)
        {
            if (nwrote < data.size())
            {
                LOG_WARN << "Write only " << nwrote << " bytes instead of " << data.size();
            }
        }
        else
        {
            nwrote = 0;
            if (errno != EWOULDBLOCK)
            {

            }
        }
    }

    // 如果没有写完，将剩余的数据写入缓冲区，并关注写事件
    if (static_cast<size_t>(nwrote) < data.size())
    {
        output_buffer_.Append(data.data() + nwrote, data.size() - nwrote);
        if (!channel_->IsWriting())
        {
            channel_->EnableWriting();
        }
    }
}

void TcpConnection::ShutDown()
{
    if (state_ == kConnected)
    {
        SetState(kDisconnecting);
        loop_->RunInLoop(std::bind(&TcpConnection::ShutDownInLoop, this));
    }
}

void TcpConnection::ForceClose()
{
    if (state_ == kConnected || state_ == kDisconnecting)
    {
        SetState(kDisconnecting);
        loop_->RunInLoop(std::bind(&TcpConnection::HandleClose, shared_from_this()));
    }
}

void TcpConnection::HandleRead()
{
    loop_->AssertInLoopThread();
    int save_errno;
    ssize_t n = input_buffer_.ReadFd(socket_->fd(), &save_errno);
    if (n > 0)
    {
        if (message_callback_)
        {
            message_callback_(shared_from_this(), &input_buffer_);
        }
    }
    else if (n == 0)
    {
        HandleClose();
    }
    else
    {
        HandleError();
    }
}

void TcpConnection::HandleClose()
{
    loop_->AssertInLoopThread();
    SetState(kDisconnected);
    channel_->DisableAll();
    std::shared_ptr<TcpConnection> guard_this(shared_from_this());

    // 1.通知用户连接已经断开
    if (connection_callback_)
    {
        connection_callback_(guard_this);
    }

    // 2. 通知TcpServer移除连接
    if (close_callback_)
    {
        close_callback_(guard_this);
    }
}

void TcpConnection::HandleError()
{
    int err = socket_->GetTcpInfoError(); // 获取具体的内核错误码

    // wrk在测试结束时会发送RST包，为了快速回收资源
    if (err == ECONNRESET || err == ETIMEDOUT) 
    {
        LOG_INFO << "TcpConnection::HandleError [" << socket_->fd()
            << "] - Expected disconnect, SO_ERROR = " << err;
    }
    else
    {
        LOG_ERROR << "TcpConnection::HandleError [" << socket_->fd()
            << "] - Actual error, SO_ERROR = " << err << " " << strerror(err);
    }

    // 关键点：异常发生，立即触发关闭流程
    HandleClose();
}

void TcpConnection::HandleWrite()
{
    loop_->AssertInLoopThread();
    if (channel_->IsWriting())
    {
        // 1. 获取待发送数据的指针和长度
        const char* data = output_buffer_.peek();
        size_t len = output_buffer_.ReadableBytes();

        // 2. 尝试写入
        ssize_t nwrote = ::write(socket_->fd(), data, len);

        if (nwrote > 0)
        {
            // 3. 将已发送的部分从缓冲区移除
            output_buffer_.retrieve(nwrote);

            // 4. 如果缓冲区空了，说明发完了，必须停止关注写事件
            // 否则 epoll 会不停地触发 EPOLLOUT 事件，导致 CPU 占用 100%
            if (output_buffer_.ReadableBytes() == 0)
            {
                channel_->DisableWriting();

                // 如果用户之前调用了 Shutdown()，状态会变成 kDisconnecting
                // 此时数据发完了，我们可以安全地关闭写端了
                if (state_ == kDisconnecting)
                {
                    ShutDownInLoop();
                }

                LOG_INFO << "TcpConnection::HandleWrite finished sending all data.";
            }
        }
        else
        {
            LOG_ERROR << "TcpConnection::HandleWrite error";
        }
    }
}

void TcpConnection::ShutDownInLoop()
{
    loop_->AssertInLoopThread();
    
    // 没有监听写事件
    if (!channel_->IsWriting())
    {
        socket_->ShutdownWrite();                                                       // 关闭写端                               
    }

    // 如果在写数据，等数据写完后，在ShutDown
}

void TcpConnection::ConnectDestroyed()
{
    loop_->AssertInLoopThread();

    if (state_ == kConnected)
    {
        SetState(kDisconnected);
        channel_->DisableAll();                                                         // 停止监听读写事件

        std::shared_ptr<TcpConnection> guard_this(shared_from_this());

        // 1.通知用户连接已经断开
        if (connection_callback_)
        {
            connection_callback_(guard_this);
        }

        // 2. 通知TcpServer移除连接
        if (close_callback_)
        {
            close_callback_(guard_this);
        }
    }

    channel_->Remove();                                                                 // 从 Poller 中彻底移除
}

TcpConnection::Entry::~Entry()
{
    if (auto conn = weak_conn_.lock())
    {
        // 避免僵尸连接导致无法收到对端返回的0，所以强制关闭连接
        LOG_INFO << "ForceClose Connection: " << conn->fd();
        conn->ForceClose();
    }
}