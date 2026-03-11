#ifndef CHANNEL_H
#define CHANNEL_H

#include "noncopyable.h"
#include <functional>
#include <memory>

class EventLoop;

class Channel : noncopyable
{
public:
	using EventCallback = std::function<void()>;
	using ReadEventCallback = std::function<void()>;

	Channel(EventLoop* loop, int fd);
	~Channel();

	// 处理事件
	void HandleEvent();
	void HandleEventWithGuard();

public:
	// 设置回调函数
	void SetReadCallback(ReadEventCallback cb) { read_callback_ = std::move(cb); }
	void SetWriteCallback(EventCallback cb) { write_callback_ = std::move(cb); }
	void SetCloseCallback(EventCallback cb) { close_callback_ = std::move(cb); }
	void SetErrorCallback(EventCallback cb) { error_callback_ = std::move(cb); }

	int fd() const { return fd_; }
	int events() const { return events_; }
	void set_revents(int revt) { revents_ = revt; }												// 由 Poller 设置，告诉内核实际发生了什么

	// 判断当前是否监听了任何事件
	bool IsNoneEvent() const { return events_ == kNoneEvent; }
	bool IsWriting() const { return events_ & kWriteEvent; }
    bool IsReading() const { return events_ & kReadEvent; }

	// 开启/关闭各类事件监听
	void EnableReading() { events_ |= kReadEvent; Update(); }
	void DisableReading() { events_ &= ~kReadEvent; Update(); }
	void EnableWriting() { events_ |= kWriteEvent; Update(); }
	void DisableWriting() { events_ &= ~kWriteEvent; Update(); }
	void DisableAll() { events_ = kNoneEvent; Update(); }										// 暂时关闭，触发Del从红黑树上删除，但是依旧保留映射

	// index 记录在 Poller 中的状态
	int index() { return index_; }
	void set_index(int idx) { index_ = idx; }

	// 返回所属的 EventLoop
	EventLoop* ownerLoop() { return loop_; }
	void Remove();																				// 删除映射关系

	// 绑定对象，确保对象生命周期安全
    void tie(const std::shared_ptr<void>& obj) { tie_ = obj; btied_ = true; }

private:
	void Update();

private:
	static const int kNoneEvent;
	static const int kReadEvent;
	static const int kWriteEvent;

	EventLoop* loop_;								// 所属的事件循环
	const int fd_;									// 封装的fd
	int events_;									// 注册的感兴趣的事件
	int revents_;									// 实际发生的事件					
	int index_;										// 在poller中的状态(未添加，已添加，删除)

	// 确保对象生命周期安全
	std::weak_ptr<void> tie_;
	bool btied_;									// 是否绑定了对象

	ReadEventCallback read_callback_;
	EventCallback write_callback_;
	EventCallback close_callback_;
	EventCallback error_callback_;
};

#endif