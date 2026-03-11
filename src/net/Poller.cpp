#include "net/Poller.h"
#include "net/Channel.h"
#include "net/EPollPoller.h"

Poller::Poller(EventLoop* loop)
	:owner_loop_(loop)
{

}

Poller::~Poller()
{

}

Poller* Poller::NewDefaultPoller(EventLoop* loop)
{
	return new EPollPoller(loop);
}

bool Poller::HasChannel(Channel* channel) const
{
	auto it = channels_.find(channel->fd());
	return it != channels_.end() && it->second == channel;
}