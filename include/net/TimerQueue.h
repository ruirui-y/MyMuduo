#ifndef TIMERQUEUE_H
#define TIMERQUEUE_H

#include <vector>
#include <set>
#include "noncopyable.h"
#include "net/Channel.h"
#include "net/Timer.h"

class EventLoop;
class TimerQueue : noncopyable
{
public:
	explicit TimerQueue(EventLoop* loop);
	~TimerQueue();

	// 添加定时器
	void AddTimer(Timer::TimerCallback cb, Timestamp when, double interval);

private:
	// 定时器到期，有自己的fd类型
	void HandleRead();

	// 获取所有到期的定时器
	std::vector<Timer*> GetExpired(Timestamp now);
	void Reset(const std::vector<Timer*>& expired, Timestamp now);

private:
	EventLoop* loop_;
	const int timerfd_;
	Channel timerfd_channel_;

	// Timer列表，按到期时间排序
	using Entry = std::pair<Timestamp, Timer*>;
	std::set<Entry> timers_;
};

#endif