#ifndef ACCEPTOR_H
#define ACCEPTOR_H

#include "net/Channel.h"
#include "net/Socket.h"									// 你需要实现一个简易的 Socket 类封装 fd
#include <functional>

class EventLoop;

class Acceptor : noncopyable
{
public:
	using NewConnectionCallback = std::function<void(int socket_fd, const std::string& peer_addr)>;

	Acceptor(EventLoop* loop, const std::string& ip, uint16_t port);
	~Acceptor();

	void SetNewConnectionCallback(NewConnectionCallback cb) { new_connection_callback_ = std::move(cb); };
	void Listen();										// 开始监听端口									
	bool listening() const { return listening_; }

private:
	void HandleRead();								

private:
	EventLoop* loop_;
	Socket accept_socket_;
	Channel accept_channel_;
	NewConnectionCallback new_connection_callback_;
	bool listening_;
};

#endif