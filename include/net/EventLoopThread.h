#ifndef EVENT_LOOP_THREAD_H
#define EVENT_LOOP_THREAD_H
#include "noncopyable.h"
#include <thread>
#include <mutex>
#include <condition_variable>

class EventLoop;
class EventLoopThread : noncopyable
{
public:
	EventLoopThread();
	~EventLoopThread();
    EventLoop* StartLoop();

private:
	void ThreadFunc();

private:
	EventLoop* loop_;
	std::thread thread_;
	std::mutex mutex_;
	std::condition_variable cond_;
};

#endif