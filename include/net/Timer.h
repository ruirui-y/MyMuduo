#ifndef TIMER_H
#define TIMER_H

#include "Timestamp.h"
#include "noncopyable.h"
#include <functional>
#include <atomic>

class Timer : noncopyable
{
public:
	using TimerCallback = std::function<void()>;

	Timer(TimerCallback cb, Timestamp when, double interval)
		:callback_(std::move(cb)),
		expiration_(when),
        interval_(interval),
		repeat_(interval > 0.0),
		sequence_(s_num_created_++)
	{

	}

	void Run() const { if (callback_) callback_(); }								// 执行回调
	
	Timestamp Expiration() const { return expiration_; }							// 获取到期事件
	bool Repeat() const { return repeat_; }											// 是否循环
	
	// 更新到期时间(循环定时器)
	void ReStart(Timestamp now);

private:
	const TimerCallback callback_;													// 到期回调
	Timestamp expiration_;															// 到期时间
	const double interval_;															// 时间间隔
	const bool repeat_;																// 是否循环
	const int64_t sequence_;														// 序列号

	static std::atomic<int64_t> s_num_created_;										// 全局计数器
};

#endif // TIMER_H