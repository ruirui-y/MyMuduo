#ifndef POLLER_H
#define POLLER_H

#include "noncopyable.h"
#include <vector>
#include <map>

class Channel;
class EventLoop;

class Poller : noncopyable
{
public:
	using ChannelList = std::vector<Channel*>;													// 用于返回发生事件的连接

	Poller(EventLoop* loop);
	virtual ~Poller();

	static Poller* NewDefaultPoller(EventLoop* loop);											// 静态工厂方法

public:
	virtual void Poll(int timeoutMs, ChannelList* active_channels) = 0;							// 必须在EventLoop所在的线程中调用，查询事件是否触发
	virtual void UpdateChannel(Channel* channel) = 0;											// 更新Channel，把fd加入epoll，或者修改状态
	virtual void RemoveChannel(Channel* channel) = 0;											// 移除Channel，把fd从epoll中移除

	// 判断Channel是否在当前的Poller中
    bool HasChannel(Channel* channel) const;

protected:
	using ChannelMap = std::map<int, Channel*>;													// 用于记录fd和Channel的对应关系
	ChannelMap channels_;

private:
	EventLoop* owner_loop_;
};

#endif