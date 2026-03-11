#include "Log/Logger.h"
#include "Log/AsyncLogging.h"
#include <iostream>
#include <sys/time.h>
#include <time.h>

// 1. 默认的输出函数：直接往屏幕喷
void defaultOutput(const char* msg, int len)
{
    fwrite(msg, 1, len, stdout);
}

// 2. 默认的刷新函数
void defaultFlush()
{
    fflush(stdout);
}

// 3. 异步刷新函数
static AsyncLogging& getAsyncLog()
{
    static AsyncLogging asyncLog("MyMuduo_Server", 4 * 1024 * 1024);
    return asyncLog;
}
void outputToAsync(const char* msg, int len)
{
    getAsyncLog().append(msg, len);
}

// 4. 全局变量，现在它们是 std::function 类型了
OutputFunc g_output = outputToAsync;
FlushFunc g_flush = defaultFlush;

LogStream& Logger::stream()
{
    // 只要有日志产生，这里自动触发启动
    static std::once_flag flag;
    std::call_once(flag, []()
        {
            getAsyncLog().start();
        });
    return impl_.stream_;
}

// 构造函数：初始化格式
Logger::Impl::Impl(const char* file, int line, LogLevel level)
    : stream_(), level_(level), line_(line), filename_(file) 
{
    formatTime(); // 先打时间
    // 这里可以根据级别打个前缀，比如 [INFO]
    const char* levelStr[] = { "[DEBUG] ", "[INFO] ","[WARN] ", "[ERROR] ", "[FATAL] "};
    stream_ << levelStr[level];
}

void Logger::Impl::formatTime() 
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    time_t seconds = tv.tv_sec;
    struct tm* tm_time = localtime(&seconds);

    char timeStr[32];
    strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S ", tm_time);
    stream_ << timeStr;
}

Logger::Logger(const char* file, int line, LogLevel level)
    : impl_(file, line, level)
{
}

// 重点！析构函数负责最后的输出
Logger::~Logger() 
{
    impl_.stream_ << " -- " << impl_.filename_ << ":" << impl_.line_ << "\n";
    const LogStream::Buffer& buf(stream().buffer());

    // 这里调用的是 std::function 的 operator()
    g_output(buf.data(), buf.length());

    if (impl_.level_ == FATAL)
    {
        g_flush();
        abort();
    }
}