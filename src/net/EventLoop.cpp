#include "net/EventLoop.h"
#include "Log/Logger.h"
#include "net/Poller.h"
#include <cassert>
#include <sys/eventfd.h>
#include <unistd.h>

int CreateEventFD()
{
	int event_fd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
	if (event_fd < 0) abort();
	return event_fd;
}

// 线程局部存储, 确保每个线程只有一个 EventLoop
__thread EventLoop* t_loopInThisThread = nullptr;

EventLoop::EventLoop()
	:thread_id_(std::this_thread::get_id()),
	poller_(Poller::NewDefaultPoller(this)),
	wakeup_fd_(CreateEventFD()),
	wake_channel_(new Channel(this, wakeup_fd_)),
	timer_queue_(new TimerQueue(this))
{
	if (t_loopInThisThread)
	{
		LOG_FATAL << "Another EventLoop exits in this thread";
	}
	else
	{
		t_loopInThisThread = this;
		wake_channel_->SetReadCallback(bind(&EventLoop::HandleRead, this));
		wake_channel_->EnableReading();												// 监听唤醒事件									
	}
}

void EventLoop::RunInLoop(Functor cb)
{
	if (IsInLoopThread())
	{
		cb();
	}
	else
	{
		QueueInLoop(std::move(cb));
	}
}

void EventLoop::QueueInLoop(Functor cb)
{
	{
		std::lock_guard<std::mutex> lock(mutex_);
		pending_functors_.push_back(std::move(cb));
	}

	/*
	* 读事件是可以保留到下一次调用epoll_wait的
	* 所以如果此时刚好处于处理任务的阶段，为避免当前的任务被延迟到下一个超时事件才唤醒
	* 如果换成if(!calling_pending_functors_) 此时没有执行任务，直接添加到任务队列就好了呀，就能到这一个轮次里了，没必要唤醒
	* if(calling_pending_functors_) 只是为了将任务添加到下一个轮次，当前轮次的任务执行完之后，立即触发读事件，接着下一轮次的任务处理，这样形成的无缝衔接
	* 目的：确保它处理完当前任务后，能立即从下一轮 poll 中跳出来处理新加入的任务
	* 
	* 如果不在本线程上也是需要唤醒的，避免任务延迟
	*/
	if (!IsInLoopThread() || calling_pending_functors_)
	{
		WakeUp();
	}
}

void EventLoop::DoPendingFunctors()
{
	std::vector<Functor> tasks;
	calling_pending_functors_ = true;

	{
        std::lock_guard<std::mutex> lock(mutex_);
        tasks.swap(pending_functors_);
	}

	for (const Functor& task : tasks)
	{
		if(task)
			task();
	}
	calling_pending_functors_ = false;
}

void EventLoop::WakeUp()
{
	uint64_t one = 1;
	::write(wakeup_fd_, &one, sizeof one);								// 只要将wakefd注册到epoll中，这里主动写数据等于触发了该fd的读事件
}

void EventLoop::HandleRead()
{
	uint64_t one = 1;
	::read(wakeup_fd_, &one, sizeof one);
}

void EventLoop::Loop()
{
	looping_ = true;
	quit_ = false;

	while (!quit_)
	{
		active_channels_.clear();

		poller_->Poll(10000, &active_channels_);
		for (Channel* channel : active_channels_)
		{
			channel->HandleEvent();
		}
        DoPendingFunctors();														// 每一轮循环后，处理跨线程投递的任务
	}

	looping_ = false;
}

void EventLoop::UpdateChannel(Channel* channel)
{
	AssertInLoopThread();
	poller_->UpdateChannel(channel);
}

void EventLoop::RemoveChannel(Channel* channel)
{
	AssertInLoopThread();
	poller_->RemoveChannel(channel);
}

void EventLoop::Quit() 
{
	quit_ = true;
	if (!IsInLoopThread()) 
	{
		WakeUp();																// 必须唤醒，否则线程可能永远卡在 poll() 里
	}
}

// 检查 Channel 是否归属当前 Poller
bool EventLoop::HasChannel(Channel* channel) 
{
	return poller_->HasChannel(channel);
}

// 定时器
void EventLoop::RunAfter(double delay, Timer::TimerCallback cb)
{
	Timestamp time(AddTime(Timestamp::now(), delay));
	timer_queue_->AddTimer(std::move(cb), time, 0);
}

void EventLoop::RunEvery(double interval, Timer::TimerCallback cb)
{
	Timestamp time(AddTime(Timestamp::now(), interval));
	timer_queue_->AddTimer(std::move(cb), time, interval);
}

void EventLoop::AbortNotInLoopThread()
{
	LOG_FATAL << "EventLoop::abortNotInLoopThread - EventLoop was created in threadId_ = "
		<< thread_id_
		<< ", current thread id = "
		<< std::this_thread::get_id();
}

EventLoop::~EventLoop()
{
	t_loopInThisThread = nullptr;
}
