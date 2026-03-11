#ifndef LOGGER_H
#define LOGGER_H

#include "LogStream.h"
#include <functional>
#include <string>
using namespace std;

// 定义日志级别，方便后期过滤
enum LogLevel 
{
    DEBUG                                   = 0,
    INFO                                    = 1,
    WARN                                    = 2,
    ERROR                                   = 3,
    FATAL                                   = 4
};

using OutputFunc = std::function<void(const char* msg, int len)>;
using FlushFunc = std::function<void()>;

class Logger 
{
public:
    Logger(const char* file, int line, LogLevel level);
    ~Logger();                                                                                              // 析构函数是日志落地的触发器

    LogStream& stream();

private:
    // 内部类，把日志的格式化逻辑包一层
    struct Impl 
    {
        Impl(const char* file, int line, LogLevel level);
        void formatTime();                                                                                  // 格式化当前时间

        LogStream stream_;
        LogLevel level_;
        int line_;
        std::string filename_;
    };

    Impl impl_;
};

// 核心宏定义：外界统一用这个
// __FILE__ 和 __LINE__ 是编译器自带的，帮你记录代码位置

#ifndef LOG_LEVEL
#define LOG_LEVEL 0  // 默认开启所有日志 (0代表DEBUG)
#endif

#define LOG_INTERNAL(level) \
    if (level < LOG_LEVEL) {} else Logger(__FILE__, __LINE__, (LogLevel)level).stream()

#define LOG_DEBUG LOG_INTERNAL(0)
#define LOG_INFO  LOG_INTERNAL(1)
#define LOG_WARN  LOG_INTERNAL(2)
#define LOG_ERROR LOG_INTERNAL(3)
#define LOG_FATAL LOG_INTERNAL(4)

#endif