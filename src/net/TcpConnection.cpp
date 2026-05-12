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
    // 设置TCP无延迟
    socket_->SetTcpNoDelay();

    channel_->SetWriteCallback([this]() { HandleWrite(); });
    channel_->SetReadCallback([this]() { HandleRead(); });
    channel_->SetCloseCallback([this]() {HandleClose(); });
    channel_->SetErrorCallback([this]() {HandleError();  });
}

TcpConnection::~TcpConnection()
{
    LOG_INFO << "TcpConnection::~TcpConnection fd=" << socket_->fd();

    // 释放ssl
    if (ssl_) {
        SSL_free(ssl_);
    }
}

void TcpConnection::ConnectEstablished()
{
    // 1. 设置状态为已连接
    SetState(kConnected);

    // 2. 绑定 Channel 与 TcpConnection 的生命周期
    // 这样在 Channel::HandleEvent 执行期间，TcpConnection 不会被析构
    channel_->tie(shared_from_this());

    // 3. 根据加密重新修正握手流程
    if (bis_ssl_)
    {
        LOG_INFO << "TcpConnection::ConnectEstablished() ssl";
        HandleSSLHandshake();
    }
    else
    {
        // 注册读事件到 Poller
        channel_->EnableReading();

        // 通知用户连接已经建立
        if (connection_callback_)
        {
            connection_callback_(shared_from_this());
        }
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
            loop_->RunInLoop([this, data]() { SendInLoop(data); });
        }
    }
}

void TcpConnection::SendInLoop(const std::string& data)
{
    loop_->AssertInLoopThread();
    ssize_t nwrote = 0;
    size_t len = data.size();
    bool faultError = false;

    // 如果之前没有数据再发，且当前的缓冲区为空，尝试直接发送
    if (!channel_->IsWriting() && output_buffer_.ReadableBytes() == 0)
    {
        if (bis_ssl_)
        {
            nwrote = SSL_write(ssl_, data.data(), len);
            if (nwrote <= 0)
            {
                int err = SSL_get_error(ssl_, nwrote);
                if (err == SSL_ERROR_WANT_WRITE || err == SSL_ERROR_WANT_READ) {
                    nwrote = 0;                                                             // 底层拥塞，下次等 epoll 唤醒
                }
                else {
                    LOG_ERROR << "SSL_write 发送致命错误: " << err;
                    faultError = true;
                }
            }
        }
        else
        {
            nwrote = ::write(socket_->fd(), data.data(), len);
            if (nwrote < 0)
            {
                if (errno != EWOULDBLOCK) {
                    LOG_ERROR << "SendInLoop Error";
                    faultError = true;
                }
                nwrote = 0;                                                                 // 底层拥塞
            }
        }

        // 写了一部分并且没有出现致命错误
        if (nwrote >= 0 && !faultError)
        {
            if (static_cast<size_t>(nwrote) < len)
            {
                LOG_WARN << "Write only " << nwrote << " bytes instead of " << len;
            }
            else if (write_complete_callback_)
            {
                // 如果全部写完，触发写完成回调
                loop_->QueueInLoop(std::bind(write_complete_callback_, shared_from_this()));
            }
        }
    }

    // 如果没有写完，将剩余的数据写入缓冲区，并关注写事件
    if (!faultError && static_cast<size_t>(nwrote) < len)
    {
        output_buffer_.Append(data.data() + nwrote, len - nwrote);
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
        auto conn = shared_from_this();
        loop_->RunInLoop([conn]() 
            { 
                conn->SetState(kDisconnecting);
                conn->ShutDownInLoop(); 
            });
    }
}

void TcpConnection::ForceClose()
{
    if (state_ == kConnected || state_ == kDisconnecting)
    {
        auto conn = shared_from_this();
        loop_->RunInLoop([conn]() 
            {
                conn->SetState(kDisconnecting);
                conn->HandleClose();
            });
    }
}

void TcpConnection::EnableSSL(SSL_CTX* ctx, bool is_server)
{
    bis_ssl_ = true;
    ssl_ = SSL_new(ctx);
    SSL_set_fd(ssl_, socket_->fd());

    if (is_server)
    {
        // 被动接客模式：等待接收 ClientHello
        SSL_set_accept_state(ssl_);
    }
    else
    {
        // 主动出击模式：准备发送 ClientHello
        SSL_set_connect_state(ssl_);
    }
}

void TcpConnection::HandleRead()
{
    loop_->AssertInLoopThread();
    int save_errno;
    ssize_t n = 0;
    
    // 判断是否是加密模式，对数据进行解密
    // OpenSSL
    if (bis_ssl_)
    {
        // openssl 不支持 readv 的分散读，直接读到一个足够大的栈内存中
        char extrabuf[65536];
        n = SSL_read(ssl_, extrabuf, sizeof(extrabuf));

        if (n > 0)
        {
            // 解密成功将明文塞进buffer
            input_buffer_.Append(extrabuf, n);
        }
        else
        {
            int err = SSL_get_error(ssl_, n);
            // 非阻塞下，没数据可读是正常的
            if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE)
            {
                n = 1;
            }
            else
            {
                LOG_ERROR << "SSL_read 发生致命错误： " << err;
                n = 0;
            }
        }
    }
    else
    {
        // 非加密模式
        n = input_buffer_.ReadFd(socket_->fd(), &save_errno);
    }
    
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

                // 执行写完成回调, 这里之所以QueueInLoop，是因为用户回调函数可能会继续调用Send，如果在Send里面也调用了write_complete_callback_
                // 栈就会一直嵌套下去，形成递归, 直到栈空间耗尽，导致程序崩溃
                if (write_complete_callback_)
                {
                    loop_->QueueInLoop(std::bind(write_complete_callback_, shared_from_this()));
                }

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

void TcpConnection::HandleSSLHandshake()
{
    loop_->AssertInLoopThread();

    int ret = SSL_do_handshake(ssl_);
    if (ret == 1)
    {
        // 握手成功
        bssl_handshake_ = true;
        LOG_INFO << "SSL handshake success";

        // 绑定业务回调
        channel_->SetWriteCallback([this]() { HandleWrite(); });
        channel_->SetReadCallback([this]() { HandleRead(); });
        channel_->SetCloseCallback([this]() {HandleClose(); });
        channel_->SetErrorCallback([this]() {HandleError();  });

        // 恢复读监听，并通知用户
        channel_->EnableReading();
        if(connection_callback_)
            connection_callback_(shared_from_this());
    }
    else
    {
        // 握手失败
        int err = SSL_get_error(ssl_, ret);
        if (err == SSL_ERROR_WANT_READ)
        {
            // OpenSSL 等待远端的加密包发过来
            LOG_DEBUG << "SSL 握手暂停：等待网卡数据 (WANT_READ)";
            channel_->EnableReading();

            // 修改读事件对应的回调为等待网卡数据的处理
            channel_->SetReadCallback(std::bind(&TcpConnection::HandleSSLHandshake, this));
        }
        else if (err == SSL_ERROR_WANT_WRITE)
        {
            // OpenSSL 需要往网卡写点握手数据
            LOG_DEBUG << "SSL 握手暂停: 等待可写事件 (WANT_WRITE)";
            channel_->EnableWriting();

            // 修改写事件对应的回调为向网卡写数据
            channel_->SetWriteCallback(std::bind(&TcpConnection::HandleSSLHandshake, this));
        }
        else
        {
            // 发生致命错误，比如证书不对，或者协议不支持
            LOG_ERROR << "SSL 握手彻底失败！err = " << err;
            ERR_print_errors_fp(stderr);
            HandleClose();
        }
    }
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