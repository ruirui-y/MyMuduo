#ifndef MYMUDUO_TCPCLIENT_H
#define MYMUDUO_TCPCLIENT_H

#include "net/EventLoop.h"
#include "net/TcpConnection.h"
#include "net/Connector.h"
#include <mutex>

class SSLContext;

class TcpClient : noncopyable
{
public:
	TcpClient(EventLoop* loop, const std::string& ip, uint16_t port, const std::string& name_arg);

	~TcpClient();

	void Connect();
	void Disconnect();
	void Stop();

	TcpConnectionPtr Connection() const
	{
		std::lock_guard<std::mutex> lock(mutex_);
		return connection_;
	}

    EventLoop* getLoop() const { return loop_; }
    const std::string& name() const { return name_; }
    void SetRetry(bool bRetry) { retry_ = bRetry; }

    void SetConnectionCallback(ConnectionCallback cb) { connection_callback_ = std::move(cb); }
    void SetMessageCallback(MessageCallback cb) { message_callback_ = std::move(cb); }
    void SetWriteCompleteCallback(WriteCompleteCallback cb) { write_complete_callback_ = std::move(cb); }

    void EnableSSL();                                                                               // 开启客户端加密

private:
    void NewConnection(int sockfd);
    void RemoveConnection(const TcpConnectionPtr& conn);

private:
    EventLoop* loop_;
    ConnectorPtr connector_;
    const std::string name_;

    ConnectionCallback connection_callback_;
    MessageCallback message_callback_;
    WriteCompleteCallback write_complete_callback_;

    std::atomic<bool> retry_;                                                                       // 是否重新连接
    std::atomic<bool> connect_;                                                                     // 业务层是否主动断开连接

    int nextConnId_;
    mutable std::mutex mutex_;
    TcpConnectionPtr connection_;

    std::shared_ptr<SSLContext> ssl_ctx_;                                                           // SSL上下文
};

#endif