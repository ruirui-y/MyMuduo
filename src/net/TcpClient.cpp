#include "net/TcpClient.h"
#include "Log/Logger.h"
#include <sys/socket.h>
#include "net/ThreadSwitcher.h"

using namespace std::placeholders;

TcpClient::TcpClient(EventLoop* loop, const std::string& ip, uint16_t port, const std::string& name_arg)
    : loop_(loop),
      connector_(new Connector(loop, ip, port)),
      name_(name_arg),
      connect_(true)
{
    connector_->SetNewConnectionCallback(
        std::bind(&TcpClient::NewConnection, this, std::placeholders::_1));
}

TcpClient::~TcpClient()
{
    TcpConnectionPtr conn;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        conn = connection_;
    }

    if (conn)
    {
        // 确保连接在所属的 loop 中被安全关闭
        auto cb = std::bind(&TcpConnection::ForceClose, conn);
        loop_->RunInLoop(cb);
    }
    else
    {
        connector_->Stop();
    }
}

void TcpClient::Connect()
{
    connect_ = true;
    connector_->Start();
}

void TcpClient::Disconnect() 
{
    connect_ = false;
    std::lock_guard<std::mutex> lock(mutex_);
    if (connection_)
    {
        connection_->ShutDown(); // 优雅关闭写端
    }
}

void TcpClient::Stop()
{
    connect_ = false;
    connector_->Stop();
}

void TcpClient::NewConnection(int sockfd)
{
    loop_->AssertInLoopThread();

    TcpConnectionPtr conn = std::make_shared<TcpConnection>(loop_, sockfd);

    // 设置回调
    conn->SetConnectionCallback(connection_callback_);
    conn->SetMessageCallback(message_callback_);
    conn->SetWriteCompleteCallback(write_complete_callback_);

    // 断开清理的回调
    conn->SetCloseCallback(std::bind(&TcpClient::RemoveConnection, this, _1));

    {
        std::lock_guard<std::mutex> lock(mutex_);
        connection_ = conn;
    }

    // 向conn提交连接初始化函数
    ThreadSwitcher::Run(loop_, connection_, &TcpConnection::ConnectEstablished);
}

void TcpClient::RemoveConnection(const TcpConnectionPtr& conn)
{
    loop_->AssertInLoopThread();

    ThreadSwitcher::Run(conn->GetLoop(), conn, &TcpConnection::ConnectDestroyed);

    {
        std::lock_guard<std::mutex> lock(mutex_);
        connection_.reset();
    }

    // 检查是否是超时断开，如果不是业务端的连接断开，自动重新连接
    if (retry_ && connect_)
    {
        LOG_INFO << "TcpClient::RemoveConnection - Reconnecting to " << connector_->GetIp() << " " << connector_->GetPort();
        // 让工兵 Connector 重新挂到 epoll 上去发起非阻塞 connect！
        connector_->Start();
    }
}