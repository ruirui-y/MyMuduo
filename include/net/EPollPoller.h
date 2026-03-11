#ifndef EPOLL_POLLER_H
#define EPOLL_POLLER_H

#include "Poller.h"
#include <vector>
#include <sys/epoll.h>

class EPollPoller : public Poller
{
public:
	EPollPoller(EventLoop* loop);
	~EPollPoller() override;

public:
	virtual void Poll(int timeoutMs, ChannelList* active_channels);
	virtual void UpdateChannel(Channel* channel);											
	virtual void RemoveChannel(Channel* channel);											

private:
	// 初始状态下，epoll监听列表的大小
	static const int k_init_event_list_size = 16;

	void FillActiveChannels(int num_evnets, ChannelList* active_channels) const;				// 填充活跃的链接
	void Update(int operation, Channel* channel);												// 更新epoll状态(epoll_ctl)

private:
	using EventList = std::vector<struct epoll_event>;
	int epoll_fd_;
	EventList events_;																			// 用于接收内核返回的事件数组
};
#endif