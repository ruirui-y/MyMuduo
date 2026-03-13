#include "net/TimerQueue.h"
#include "net/EventLoop.h"
#include <sys/timerfd.h>
#include <unistd.h>
#include <string.h>

int createTimerfd() 
{
    int timerfd = ::timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    return timerfd;
}

// 设置timerfd的到期时间
void ResetTimerfd(int timerfd, Timestamp expiration)
{
    struct itimerspec newValue;
    struct itimerspec oldValue;
    ::memset(&newValue, 0, sizeof newValue);
    
    int64_t microseconds = expiration.microSecondsSinceEpoch() - Timestamp::now().microSecondsSinceEpoch();
    if (microseconds < 100) microseconds = 100;

    newValue.it_value.tv_sec = static_cast<time_t>(microseconds / 1000000);
    newValue.it_value.tv_nsec = static_cast<long>((microseconds % 1000000) * 1000);

    ::timerfd_settime(timerfd, 0, &newValue, &oldValue);
}

TimerQueue::TimerQueue(EventLoop* loop)
    : loop_(loop),
    timerfd_(createTimerfd()),
    timerfd_channel_(loop, timerfd_)
{
    timerfd_channel_.SetReadCallback([this]() { HandleRead(); });
    timerfd_channel_.EnableReading();                                                            // 开启定时器的监听
}

void TimerQueue::AddTimer(Timer::TimerCallback cb, Timestamp when, double interval) 
{
    Timer* timer = new Timer(std::move(cb), when, interval);
    loop_->RunInLoop([this, timer]() 
        {
            bool earliestChanged = timers_.empty() || timer->Expiration() < timers_.begin()->first;
            timers_.insert({ timer->Expiration(), timer });
            if (earliestChanged) 
            {
                ResetTimerfd(timerfd_, timer->Expiration());
            }
        });
}

void TimerQueue::HandleRead() 
{
    Timestamp now(Timestamp::now());
    uint64_t readed;
    ::read(timerfd_, &readed, sizeof(readed));                                                  // 清除 timerfd 的事件标志

    std::vector<Timer*> expired = GetExpired(now);                                              // 获取所有超时事件
    for (auto& timer : expired) 
    {
        timer->Run();
    }
    Reset(expired, now);
}

std::vector<Timer*> TimerQueue::GetExpired(Timestamp now) 
{
    std::vector<Timer*> expired;
    auto it = timers_.lower_bound({ now, reinterpret_cast<Timer*>(UINTPTR_MAX) });              // 获取第一个不小于now的迭代器

    for (auto i = timers_.begin(); i != it; ++i) 
    {
        expired.push_back(i->second);
    }
                       
    timers_.erase(timers_.begin(), it);
    return expired;
}

TimerQueue::~TimerQueue() 
{
    timerfd_channel_.DisableAll();
    timerfd_channel_.Remove();
    ::close(timerfd_);
    for (const auto& entry : timers_) 
    {
        delete entry.second;
    }
}

void TimerQueue::Reset(const std::vector<Timer*>& expired, Timestamp now)
{
    for (auto& timer : expired)
    {
        // 如果定时器是循环的
        if (timer->Repeat()) 
        {
            timer->ReStart(now);
            timers_.insert({ timer->Expiration(), timer });
        }
        else
        {
            delete timer;
        }
    }

    // set自动重排，重新设置到期时间
    if (!timers_.empty()) 
    {
        ResetTimerfd(timerfd_, timers_.begin()->first);
    }
}