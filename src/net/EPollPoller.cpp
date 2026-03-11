#include "net/EPollPoller.h"
#include "Log/Logger.h"
#include "net/Channel.h"
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <cassert>

// Channel的状态标识
const int kNew = 1;											// 未添加进epoll
const int kAdded = 2;										// 已添加进epoll
const int kDeleted = 3;										// 已从epoll中删除


EPollPoller::EPollPoller(EventLoop* loop)
	: Poller(loop),
	epoll_fd_(::epoll_create1(EPOLL_CLOEXEC)),
	events_(k_init_event_list_size)
{
	if (epoll_fd_ < 0)
	{
		LOG_FATAL << "epoll_create1 error";
	}
}

EPollPoller::~EPollPoller()
{
	::close(epoll_fd_);
}

void EPollPoller::Poll(int timeoutMs, ChannelList* active_channels)
{
	int num_events = ::epoll_wait(epoll_fd_, &*events_.begin(), static_cast<int>(events_.size()), timeoutMs);
	int save_err = errno;

	if (num_events > 0)
	{
		FillActiveChannels(num_events, active_channels);

		// 如果一轮下来16个位置都满了，自动扩容
		if (num_events == static_cast<int>(events_.size()))
		{
			events_.resize(events_.size() * 2);
		}
	}
	else if (num_events == 0)
	{
		// 超时
	}
	else
	{
		// 报错，如果不是信号中断，打印错误日志
		if (save_err != EINTR)
		{
			save_err = errno;
            LOG_ERROR << "epoll_wait error";
		}
	}
}

void EPollPoller::UpdateChannel(Channel* channel)
{
	const int index = channel->index();															// index 在这里被复用来储存状态
	const int fd = channel->fd();
	if (index == kNew || index == kDeleted)
	{
		// 第一次加入
		if (index == kNew)
		{
			assert(channels_.find(fd) == channels_.end());
			channels_[channel->fd()] = channel;
		}

		// 为避免占用红黑树资源的暂时关闭
		if (index == kDeleted)
		{
			assert(channels_.find(fd) != channels_.end());
			assert(channels_[fd] == channel);
		}

		channel->set_index(kAdded);
		Update(EPOLL_CTL_ADD, channel);
	}
	else
	{
		assert(channels_.find(fd) != channels_.end());
		assert(channels_[fd] == channel);
		// 如果不监听任何事件，避免资源浪费，从epoll中删除，但是不关闭映射
		if (channel->IsNoneEvent())
		{
			Update(EPOLL_CTL_DEL, channel);
            channel->set_index(kDeleted);
		}
		else
		{
			// 已经在 epoll 里了：使用 MOD
			Update(EPOLL_CTL_MOD, channel);
		}
	}
}

void EPollPoller::RemoveChannel(Channel* channel)
{
	int fd = channel->fd();
	channels_.erase(fd);

	int index = channel->index();
	if (index == kAdded)
	{
		Update(EPOLL_CTL_DEL, channel);
	}

	channel->set_index(kNew);
}

// 把内核返回的epoll_event 转换成 Channel
void EPollPoller::FillActiveChannels(int num_evnets, ChannelList* active_channels) const
{
	for (int i = 0; i < num_evnets; ++i)
	{
		Channel* channel = static_cast<Channel*>(events_[i].data.ptr);
		channel->set_revents(events_[i].events);												// 告诉Channel发生了什么事件
		active_channels->push_back(channel);													// EveentLoop会调用Channel的HandleEvent
	}
}

void EPollPoller::Update(int operation, Channel* channel)
{
	struct epoll_event event;
	memset(&event, 0, sizeof(event));

	event.events = channel->events();
	event.data.ptr = channel;

	int fd = channel->fd();
	if (::epoll_ctl(epoll_fd_, operation, fd, &event) < 0)
	{
		if (operation == EPOLL_CTL_DEL)
		{
			LOG_ERROR << "epoll_ctl del error";
		}
		else
		{
			LOG_FATAL << "epoll_ctl add/mod error";
		}
	}
}