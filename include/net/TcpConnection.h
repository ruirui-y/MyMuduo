#ifndef TCPCONNECTION_H
#define TCPCONNECTION_H

#include "noncopyable.h"
#include "net/Channel.h"
#include "net/Socket.h"
#include "net/Buffer.h"
#include <memory>
#include <string>
#include <functional>
#include <any>

class EventLoop;
class TcpConnection;
using TcpConnectionPtr = std::shared_ptr<TcpConnection>;
using ConnectionCallback = std::function<void(const TcpConnectionPtr&)>;
using MessageCallback = std::function<void(const TcpConnectionPtr&, Buffer*)>;
using CloseCallback = std::function<void(const TcpConnectionPtr&)>;
using WriteCompleteCallback = std::function<void(const TcpConnectionPtr&)>;									// 数据被彻底写进网卡（OutputBuffer 被清空）时的回调

class TcpConnection : public std::enable_shared_from_this<TcpConnection>, noncopyable
{
public:
	struct Entry {
		std::weak_ptr<TcpConnection> weak_conn_;
		Entry(const std::weak_ptr<TcpConnection>& weakConn) : weak_conn_(weakConn) {}

		// 关键点：析构函数必须在 .cpp 中实现，因为此时需要知道 TcpConnection 的完整定义
		~Entry();
	};
	using EntryPtr = std::shared_ptr<Entry>;

public:
	enum StateE { kDisconnected, kConnecting, kConnected, kDisconnecting };

	TcpConnection(EventLoop* loop, int socket_fd);
	~TcpConnection();

	// 连接管理
	void ConnectEstablished();													// 连接建立时调用
	void ConnectDestroyed();													// 连接销毁时调用
	void SetState(StateE s) { state_ = s; }										// 连接状态

	// 连接状态
	bool Connected() const { return state_ == kConnected; }
	bool DisConnected() const { return state_ == kDisconnected; }

	int fd() const { return socket_->fd(); }

	// 消息回调
	void SetMessageCallback(MessageCallback cb) { message_callback_ = std::move(cb); }
	void SetConnectionCallback(const ConnectionCallback& cb) { connection_callback_ = cb; }
	void SetCloseCallback(const CloseCallback& cb) { close_callback_ = cb; }
	void SetWriteCompleteCallback(WriteCompleteCallback cb) { write_complete_callback_ = std::move(cb); }

	// 发送数据
	void Send(const std::string& data);
	void SendInLoop(const std::string& data);

	// 获取loop
	EventLoop* GetLoop() { return loop_; }

	// 手动断开连接
	/*
	* 关闭写端，读端检测到写端关闭，待数据读完之后，会返回给服务器0
	* 服务器read 0，会调用HandleClose关闭连接
	*/
	void ShutDown();

	/*
	* 强制调用HandleClose，直接从loop中移除channel
	*/
	void ForceClose();															// 强制关闭

	// 时间轮
	void SetEntry(const EntryPtr& entry) { entry_ = entry; }

	// 新增Context，为了绑定连接的属性，比如user_id
	void SetContext(const std::any& context) { context_ = context; }
	const std::any& GetContext() const { return context_; }
	std::any* GetMutableContext() { return &context_; }

private:
	void HandleRead();															// 处理读事件
	void HandleWrite();															// 处理写事件	
	void HandleClose();															// 处理关闭事件
    void HandleError();															// 处理错误事件
	void ShutDownInLoop();														// 在loop中关闭连接

private:
	EventLoop* loop_;															// 所属的EventLoop										
	std::unique_ptr<Socket> socket_;
	std::unique_ptr<Channel> channel_;
	
	MessageCallback message_callback_;
    ConnectionCallback connection_callback_;
	CloseCallback close_callback_;
	WriteCompleteCallback write_complete_callback_;

	Buffer output_buffer_;														// 写数据缓冲
	Buffer input_buffer_;														// 读数据缓冲

	StateE state_;																// 连接状态

	std::weak_ptr<Entry> entry_;												// conn的包装器，用于挪移到时间轮的桶子里

	std::any context_;															// 万能口袋
};

#endif