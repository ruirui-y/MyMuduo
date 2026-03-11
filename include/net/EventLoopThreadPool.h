#ifndef EVENTLOOPTHREADPOOL_H
#define EVENTLOOPTHREADPOOL_H

#include "EventLoopThread.h"
#include <vector>
#include <memory>
#include <string>

class EventLoopThreadPool : noncopyable 
{
public:
	EventLoopThreadPool(EventLoop* baseLoop);
	~EventLoopThreadPool();

    void SetThreadNum(int num_threads) { num_threads_ = num_threads; }

    void Start();
    EventLoop* GetNextLoop();                                                           // 获取下一个Loop                    

private:
    EventLoop* base_loop_;                                                              // 主线程的 Loop
    int num_threads_;
    int next_;                                                                          // 轮询计数器
    std::vector<std::unique_ptr<EventLoopThread>> threads_;
    std::vector<EventLoop*> loops_;                                                     // 存储所有线程对应的 Loop 指针
};

#endif