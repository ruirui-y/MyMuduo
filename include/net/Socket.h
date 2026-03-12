#ifndef SOCKET_H
#define SOCKET_H

#include "noncopyable.h"
#include <string>

class Socket : noncopyable
{
public:
	explicit Socket(int socket_fd)
		:socket_fd_(socket_fd)
	{
	}

	~Socket();

	int fd() const { return socket_fd_; }

	void BindAddress(const std::string& ip, uint16_t port);								// 绑定地址与端口
	void Listen();																		// 监听
	int Accept();																		// 接受连接				
	void ShutdownWrite();																// 关闭写端fd
	int GetTcpInfoError() const;														// 获取TCP错误信息
	void SetTcpNoDelay();																// 设置TCP无延迟
private:
	const int socket_fd_;
};

#endif