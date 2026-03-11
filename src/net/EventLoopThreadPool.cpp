#include "net/EventLoopThreadPool.h"
#include "Log/Logger.h"

EventLoopThreadPool::EventLoopThreadPool(EventLoop* baseLoop)
    : base_loop_(baseLoop),
    num_threads_(0),
    next_(0)
{

}

EventLoopThreadPool::~EventLoopThreadPool() 
{
    // unique_ptr 会自动清理 EventLoopThread
}

void EventLoopThreadPool::Start() 
{
    for (int i = 0; i < num_threads_; ++i)
    {
        auto t = std::make_unique<EventLoopThread>();
        loops_.push_back(t->StartLoop());
        threads_.push_back(std::move(t));
    }
}

EventLoop* EventLoopThreadPool::GetNextLoop() 
{
    EventLoop* loop = base_loop_;                                                               // 默认返回主线程 Loop

    if (!loops_.empty()) 
    {
        loop = loops_[next_];
        next_ = (next_ + 1) % loops_.size();                                                    // 轮询算法
    }
    return loop;
}