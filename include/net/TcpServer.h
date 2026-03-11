#ifndef TCPSERVER_H
#define TCPSERVER_H

#include "net/Acceptor.h"
#include "net/TcpConnection.h"
#include "net/EventLoop.h"
#include "net/EventLoopThreadPool.h"
#include <map>
#include <memory>
#include <unordered_set>

class TcpServer : noncopyable
{
public:
	TcpServer(EventLoop* loop, const std::string& ip, uint16_t port, uint16_t conn_time_out);

	~TcpServer();

	void Start(int thread_num);
	void SetMessageCallback(TcpConnection::MessageCallback cb) { message_cb_ = cb; }
	void SetConnectionCallback(const TcpConnection::ConnectionCallback& cb) { connection_callback_ = cb; }

private:
	void NewConnection(int sockfd, const std::string& peerAddr);									// 处理新连接的回调
	void RemoveConnection(const std::shared_ptr<TcpConnection>& conn);
	void RemoveConnectionInLoop(const std::shared_ptr<TcpConnection>& conn);

	void Tick();																					// 时间轮的回调

private:
	void RefreshEntry(TcpConnection::EntryPtr entry);												// 刷新连接在时间轮里的超时索引

private:
	EventLoop* loop_ = nullptr;
	std::unique_ptr<Acceptor> acceptor_;

	// 连接池
	std::map<int, std::shared_ptr<TcpConnection>> connections_;

	TcpConnection::MessageCallback message_cb_;
    TcpConnection::ConnectionCallback connection_callback_;

	// 线程池
	std::unique_ptr<EventLoopThreadPool> thread_pool_;
	using TcpConnectionPtr = std::shared_ptr<TcpConnection>;

	// 时间轮
	using Bucket = std::unordered_set<TcpConnection::EntryPtr>;
	using TimingWheel = std::vector<Bucket>;

	TimingWheel wheel_;																				// 轮子的大小是连接的超时时间
	int wheel_size_;
	int wheel_curr_ = 0;
};

#endif