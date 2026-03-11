#include "net/Channel.h"
#include "net/EventLoop.h"
#include <sys/epoll.h>

const int Channel::kNoneEvent = 0;
const int Channel::kReadEvent = EPOLLIN | EPOLLPRI;
const int Channel::kWriteEvent = EPOLLOUT;

Channel::Channel(EventLoop* loop, int fd)
    : loop_(loop),
    fd_(fd),
    events_(0),
    revents_(0),
    index_(1),
    btied_(false)
{
	
}

Channel::~Channel()
{

}

void Channel::Remove()
{
    loop_->RemoveChannel(this);
}

// 当fd所在的EventLoop想要改变监听事件时调用
void Channel::Update()
{
    loop_->UpdateChannel(this);
}

void Channel::HandleEvent()
{
    if (btied_)
    {
        std::shared_ptr<void> gurd = tie_.lock();
        if (gurd)
        {
            HandleEventWithGuard();
        }
    }
    else
    {
        HandleEventWithGuard();
    }
}

void Channel::HandleEventWithGuard()
{
    // 1. 发生错误
    if (revents_ & EPOLLERR)
    {
        if (error_callback_) error_callback_();
    }

    // 2. 对端关闭连接
    if ((revents_ & EPOLLHUP) && !(revents_ & EPOLLIN))
    {
        if (close_callback_) close_callback_();
    }

    // 3. 可读事件(紧急数据)
    if (revents_ & (EPOLLIN | EPOLLPRI | EPOLLRDHUP))
    {
        if (read_callback_) read_callback_();
    }

    // 4. 可写事件
    if (revents_ & EPOLLOUT)
    {
        if (write_callback_) write_callback_();
    }
}