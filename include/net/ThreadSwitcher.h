#ifndef THREAD_SWITCHER_H
#define THREAD_SWITCHER_H
#include "net/EventLoop.h"

class ThreadSwitcher
{
public:
    template<typename T, typename Method, typename... Args>
    static void Run(EventLoop* loop, std::weak_ptr<T> weak_obj, Method method, Args&&... args)
    {
        if (loop->IsInLoopThread())
        {
            if (auto obj = weak_obj.lock()) 
            {
                (obj.get()->*method)(std::forward<Args>(args)...);
            }
            return;
        }

        // 否则派发到 Loop 线程
        loop->QueueInLoop([weak_obj, method, args...]() mutable 
            {
                // 必须在执行时刻尝试 lock，确保对象存活
                if (auto obj = weak_obj.lock()) 
                {
                    (obj.get()->*method)(args...);
                }
            });
    }

    // 智能指针版本
    template<typename T, typename Method, typename... Args>
    static void Run(EventLoop* loop, std::shared_ptr<T> shared_obj, Method method, Args&&... args)
    {
        if (loop->IsInLoopThread())
        {
            (shared_obj.get()->*method)(std::forward<Args>(args)...);
            return;
        }

        // 否则派发到 Loop 线程
        loop->QueueInLoop([shared_obj, method, args...]() mutable
            {
                (shared_obj.get()->*method)(args...);
            });
    }

    // 裸指针，需要自己确保对象生命周期
    template<typename T, typename Method, typename... Args>
    static void Run(EventLoop* loop, T* obj, Method method, Args&&... args) {
        if (loop->IsInLoopThread()) {
            (obj->*method)(std::forward<Args>(args)...);
        }
        else {
            loop->QueueInLoop([obj, method, args...]() {
                (obj->*method)(args...);
                });
        }
    }

    // =================================================================
    // Lambda 表达式 / Functor 版本
    // 允许直接投递任意可调用对象，极大地提升了灵活性
    // =================================================================
    template<typename Func>
    static void Run(EventLoop* loop, Func&& func)
    {
        if (loop->IsInLoopThread())
        {
            // 如果已经在当前线程，直接完美转发并执行
            std::forward<Func>(func)();
        }
        else
        {
            // 否则派发到 Loop 线程排队执行
            loop->QueueInLoop(std::forward<Func>(func));
        }
    }
};

#endif