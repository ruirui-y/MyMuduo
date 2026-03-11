#ifndef EVENTLOOP_H
#define EVENTLOOP_H

#include "noncopyable.h"
#include "net/Poller.h"
#include "net/Channel.h"
#include "net/TimerQueue.h"
#include <vector>
#include <memory>
#include <atomic>
#include <thread>
#include <mutex>

class EventLoop : noncopyable 
{
public:
    using Functor = std::function<void()>;

    EventLoop();
    ~EventLoop();

    void Loop();                                                    // 开启循环，此函数永远不会返回
    void Quit();                                                    // 退出循环

    void RunInLoop(Functor cb);                                     // 当前线程上对象投递的任务，直接执行
    void QueueInLoop(Functor cb);                                   // 别的线程上的对象，投递的任务放到任务放到队列中，通过wakefd唤醒epoll_wait，再统一执行
    void WakeUp();                                                  // 唤醒正在监听的epoll

    // 供 channel 使用的接口
    void UpdateChannel(Channel* channel);
    void RemoveChannel(Channel* channel);
    bool HasChannel(Channel* channel);

    // 判断当前线程是否是创建这个EventLoop的线程
    bool IsInLoopThread() const { return thread_id_ == std::this_thread::get_id(); }
    void AssertInLoopThread() { if (!IsInLoopThread()) AbortNotInLoopThread(); }

    // 定时器
    void RunAfter(double delay, Timer::TimerCallback cb);
    void RunEvery(double interval, Timer::TimerCallback cb);

private:
    void AbortNotInLoopThread();                                    // 在错误的线程上调用就终止
    void HandleRead();                                              // 处理wakefd_触发的唤醒
    void DoPendingFunctors();                                       // 执行队列中的任务

private:
    using ChannelList = std::vector<Channel*>;

    std::atomic<bool> looping_{ false };
    std::atomic<bool> quit_{ false };
    std::atomic<bool> calling_pending_functors_{ false };           // 是否在执行回调

    const std::thread::id thread_id_;                               // 创建该Loop的线程ID

    std::unique_ptr<Poller> poller_;                                // 核心部件
    int wakeup_fd_;                                                 // 唤醒fd
    std::unique_ptr<Channel> wake_channel_;                         // 唤醒channel
    ChannelList active_channels_;                                   // 当前活跃的事件列表
    
    std::mutex mutex_;
    std::vector<Functor> pending_functors_;                         // 待处理的任务队列

    std::unique_ptr<TimerQueue> timer_queue_;                       // 定时器队列                                     
};
#endif