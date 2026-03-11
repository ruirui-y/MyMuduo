#include "net/Timer.h"

std::atomic<int64_t> Timer::s_num_created_{ 0 };

void Timer::ReStart(Timestamp now)
{
    if (repeat_)
    {
        // 计算下一次触发时间：当前触发时间 + 间隔
        expiration_ = AddTime(now, interval_);
    }
    else
    {
        // 非循环定时器过期后，设置为无效时间
        expiration_ = Timestamp::invalid();
    }
}