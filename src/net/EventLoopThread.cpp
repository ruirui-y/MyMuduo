#include "net/EventLoopThread.h"
#include "net/EventLoop.h"

EventLoopThread::EventLoopThread()
	: loop_(nullptr),
	mutex_(),
	cond_()
{

}

EventLoopThread::~EventLoopThread()
{
	if (loop_ != nullptr)
	{
		loop_->Quit();												// 停止Loop
		if (thread_.joinable())
		{
			thread_.join();
		}
	}
}

EventLoop* EventLoopThread::StartLoop()
{
	thread_ = std::thread(&EventLoopThread::ThreadFunc, this);
	EventLoop* loop = nullptr;
	{
        std::unique_lock<std::mutex> lock(mutex_);
		// 等待loop_初始化完成
        cond_.wait(lock, [this]() { return loop_ != nullptr; });
		loop = loop_;
	}
	return loop;
}

void EventLoopThread::ThreadFunc()
{
	// 创建一个loop对象
	EventLoop loop;

	{
		std::unique_lock<std::mutex> lock(mutex_);
		loop_ = &loop;
		cond_.notify_one();													// 通知 startLoop 的主线程
	}

    loop.Loop();															// 开启事件循环

	// 退出循环清理资源
	std::unique_lock<std::mutex> lock(mutex_);
	loop_ = nullptr;
}