#ifndef MYMUDUO_CONNECTOR_H
#define MYMUDUO_CONNECTOR_H

#include "net/EventLoop.h"
#include "net/Channel.h"
#include <functional>
#include <memory>
#include <atomic>
class Connector;
using ConnectorPtr = std::shared_ptr<Connector>;

class Connector : public std::enable_shared_from_this<Connector>
{
public:
	using NewConnectionCallback = std::function<void(int sockfd)>;

	Connector(EventLoop* loop, const std::string& ip, uint16_t port);
	~Connector();

	void SetNewConnectionCallback(const NewConnectionCallback& cb) { new_connection_callback_ = cb; }
	void Start();																							// 启动连接
	void Stop();																							// 停止连接

	std::string GetIp() const { return ip_; }
	int GetPort() const { return port_; }

private:
	void StartInLoop();
	void StopInLoop();
	void Connect();
	void Connecting(int sockfd);
	void HandleWrite(); // 核心：epoll 侦测到连接建立时的可写事件
	void HandleError();
	void Retry(int sockfd);
	int RemoveAndResetChannel();
	void ResetChannel();

private:
	enum States { kDisconnected, kConnecting, kConnected };
	void SetState(States s) { state_ = s; }

private:
	EventLoop* loop_;
	std::string  ip_;
	uint16_t port_;

	std::atomic<bool> connect_;
	std::atomic<States> state_;
	std::unique_ptr<Channel> channel_; // 专门用来监听 connect 状态的通道
	NewConnectionCallback new_connection_callback_;
};

#endif