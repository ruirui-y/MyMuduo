#ifndef TIMESTAMP_H
#define TIMESTAMP_H

#include <iostream>
#include <string>
#include <sys/time.h>

class Timestamp
{
public:
    Timestamp() : microSecondsSinceEpoch_(0) {}
    explicit Timestamp(int64_t microSecondsSinceEpoch)
        : microSecondsSinceEpoch_(microSecondsSinceEpoch) 
    {
    }

    static Timestamp now() 
    {
        struct timeval tv;
        gettimeofday(&tv, NULL);
        return Timestamp(tv.tv_sec * 1000000 + tv.tv_usec);
    }

    int64_t microSecondsSinceEpoch() const { return microSecondsSinceEpoch_; }

    // 供 std::set 排序使用
    bool operator<(const Timestamp& other) const 
    {
        return microSecondsSinceEpoch_ < other.microSecondsSinceEpoch_;
    }

    // 用于逻辑判断的无效时间
    static Timestamp invalid() { return Timestamp(0); }

    // 格式化输出
    std::string toString() const 
    {
        char buf[64] = { 0 };
        int64_t seconds = microSecondsSinceEpoch_ / 1000000;
        int64_t microseconds = microSecondsSinceEpoch_ % 1000000;
        struct tm tm_time;
        localtime_r((time_t*)&seconds, &tm_time);
        snprintf(buf, sizeof(buf), "%4d-%02d-%02d %02d:%02d:%02d.%06d",
            tm_time.tm_year + 1900, tm_time.tm_mon + 1, tm_time.tm_mday,
            tm_time.tm_hour, tm_time.tm_min, tm_time.tm_sec,
            static_cast<int>(microseconds));
        return std::string(buf);
    }

private:
    int64_t microSecondsSinceEpoch_;
};

inline Timestamp AddTime(Timestamp timestamp, double seconds) 
{
    int64_t delta = static_cast<int64_t>(seconds * 1000000);
    return Timestamp(timestamp.microSecondsSinceEpoch() + delta);
}

#endif