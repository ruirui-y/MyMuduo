#include "base/ThreadPool.h"

ThreadPool::ThreadPool(const std::string& nameArg)
    : mutex_(),
    cond_(),
    name_(nameArg),
    running_(false)
{
}

ThreadPool::~ThreadPool()
{
    if (running_)
    {
        stop();
    }
}

void ThreadPool::start(int numThreads)
{
    running_ = true;
    threads_.reserve(numThreads);
    for (int i = 0; i < numThreads; ++i)
    {
        // 唤醒新线程，并让它执行 runInThread 这个死循环
        threads_.emplace_back(new std::thread(std::bind(&ThreadPool::runInThread, this)));
    }
}

void ThreadPool::stop()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        running_ = false;
        // 摇响所有的铃铛，把沉睡的线程全部叫醒，准备下班！
        cond_.notify_all();
    }

    // 等待所有苦力把手头的活干完，安全销毁
    for (auto& thr : threads_)
    {
        if (thr->joinable())
        {
            thr->join();
        }
    }
}

void ThreadPool::run(Task task)
{
    if (threads_.empty())
    {
        // 退化处理：如果没开线程，就在当前调用者的线程直接同步执行
        task();
    }
    else
    {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.push(std::move(task)); // 把任务塞进仓库
        cond_.notify_one();           // 摇响一次铃铛，随机唤醒一个线程来干活
    }
}

void ThreadPool::runInThread()
{
    while (running_)
    {
        Task task;
        {
            // 1. 获取锁，准备进入仓库
            std::unique_lock<std::mutex> lock(mutex_);

            // 2. 核心防御：如果仓库是空的，且还没下班，就交出锁，进入沉睡！
            // 等待 cond_.notify_one() 的唤醒
            while (queue_.empty() && running_)
            {
                cond_.wait(lock);
            }

            // 3. 如果是因为 stop() 下班被唤醒的，且任务执行完了，就立刻结束死循环
            if (!running_ && queue_.empty())
            {
                break;
            }

            // 4. 从仓库取出一个任务
            if (!queue_.empty())
            {
                task = queue_.front();
                queue_.pop();
            }
        } // 5. 极其关键：离开大括号，释放 mutex_ 锁！让其他线程可以继续去仓库取货！

        // 6. 开始极其耗时的写磁盘动作... (此时不占有锁，绝对不阻塞其他线程！)
        if (task)
        {
            task();
        }
    }
}