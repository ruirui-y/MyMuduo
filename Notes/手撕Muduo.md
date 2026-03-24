# 查看代码行数
<!-- 这是一张图片，ocr 内容为： -->
![](https://cdn.nlark.com/yuque/0/2026/png/54962239/1772690452912-5728526f-e95e-4975-835b-5e49e2b86a1f.png)

base下面有3564行

<!-- 这是一张图片，ocr 内容为： -->
![](https://cdn.nlark.com/yuque/0/2026/png/54962239/1772690429702-d013151f-386f-4399-857d-071e6cfe5bb5.png)

net有6087行

<!-- 这是一张图片，ocr 内容为： -->
![](https://cdn.nlark.com/yuque/0/2026/png/54962239/1772690470143-ecccb988-91d8-4bf6-9020-e4defac9403f.png)



加载一起算是有1w行了，是一个挺大的项目





<!-- 这是一张图片，ocr 内容为： -->
![](https://cdn.nlark.com/yuque/0/2026/png/54962239/1772850407371-11482ea5-03e2-4d37-a4e4-5186441eb747.png)



# 项目架构
细分为网络模块和日志模块，网络模块又分为好几个阶段，分别是单线程模块，多线程模块，以及定时器模块后面就是生产环境中进行测试



## 日志模块
Logger提供方法

LogStream负责重载<<，让使用起来更加方便

FixedBuffer是缓冲区

AsyncLogging负责异步写入，避免阻塞业务模块



是一个比较完善的异步模块，按道理来说也不会存在有系统能在3s钟写入1000条日志

就算高并发的环境，在第一个缓冲区满的那一刻就会将立即唤醒写线程，执行写入操作

<!-- 这是一张图片，ocr 内容为： -->
![](https://cdn.nlark.com/yuque/0/2026/png/54962239/1773041822819-8ae4bca9-9ad3-4d5d-8cd7-134ed89a3695.png)

### FixedBuffer
```cpp
#pragma once

#include "noncopyable.h"
#include <cstring>
using namespace std;

template<int SIZE>
class FixedBuffer : noncopyable {
public:
	FixedBuffer() : cur_(data_) {}

	// 填充数据
	void append(const char* buf, size_t len) 
	{
		if (avail() > len) 
		{
			memcpy(cur_, buf, len);
			cur_ += len;
		}
	}

	const char* data() const { return data_; }
	int length() const { return static_cast<int>(cur_ - data_); }										// 已用数据
	int avail() const { return static_cast<int>(end() - cur_); }										// 可用数据
	void reset() { cur_ = data_; }

	const char* current() const { return cur_; }														// 获取当前指针位置
	void add(size_t len) { cur_ += len; }																// 手动移动指针

private:
	const char* end() const { return data_ + sizeof data_; }

private:
	char data_[SIZE];
	char* cur_;
};
```



### LogStream
一次刷新一条日志到FixedBuffer缓冲区中

```cpp
#ifndef LOG_STREAM_H
#define LOG_STREAM_H
#include "noncopyable.h"
#include "FixedBuffer.h"
#include <string>
#include <string.h>
#include <sstream>
#include <thread>

const int kSmallBuffer = 4000;							// 4KB，存单条日志绰绰有余
const int kLargeBuffer = 4000 * 1000;					// 4MB，将来做异步日志写文件用

class LogStream : noncopyable
{
public:
	using Buffer = FixedBuffer<kSmallBuffer>;

	LogStream& operator<<(bool v)
	{
		buffer_.append(v ? "1" : "0" , 1);
		return *this;
	}

	LogStream& operator<<(short);
	LogStream& operator<<(unsigned short);
	LogStream& operator<<(int);
	LogStream& operator<<(unsigned int);
	LogStream& operator<<(long);
	LogStream& operator<<(unsigned long);
	LogStream& operator<<(long long);
	LogStream& operator<<(unsigned long long);


    LogStream& operator<<(float v) 
    {
        *this << static_cast<double>(v);
        return *this;
    }
    LogStream& operator<<(double);

    LogStream& operator<<(char v) 
    {
        buffer_.append(&v, 1);
        return *this;
    }

    LogStream& operator<<(const char* str) 
    {
        if (str) 
        {
            buffer_.append(str, strlen(str));
        }
        else {
            buffer_.append("(null)", 6);
        }
        return *this;
    }

    LogStream& operator<<(const std::string& v) {
        buffer_.append(v.c_str(), v.size());
        return *this;
    }

    // 打印线程id
    LogStream& operator<<(std::thread::id id) 
    {
        std::stringstream ss;
        ss << id;
        std::string s = ss.str();
        append(s.c_str(), s.size());
        return *this;
    }

    // 打印指针地址
    LogStream& operator<<(const void* p) 
    {
        uintptr_t v = reinterpret_cast<uintptr_t>(p);
        // 直接利用你现有的处理 unsigned long long 的重载
        return *this << v;
    }

    void append(const char* data, int len) { buffer_.append(data, len); }
    const Buffer& buffer() const { return buffer_; }
    void resetBuffer() { buffer_.reset(); }

private:
    template<typename T>
    void formatInteger(T);

	Buffer buffer_;
	static const int kMaxNumericSize = 48;
};

#endif
```



```cpp
#include "Log/LogStream.h"
#include <algorithm>

// 1. 定义数字映射表，避免频繁的除法和取模逻辑判断
const char digits[] = "9876543210123456789";
const char* zero = digits + 9;

// 2. 高性能整数转字符串模版
template<typename T>
void LogStream::formatInteger(T v) {
    if (buffer_.avail() >= kMaxNumericSize) {
        char* buf = const_cast<char*>(buffer_.current());
        char* p = buf;
        T i = v;

        // 核心转换逻辑
        do {
            int lsd = static_cast<int>(i % 10);
            i /= 10;
            *p++ = zero[lsd];
        } while (i != 0);

        if (v < 0) {
            *p++ = '-';
        }
        *p = '\0';
        std::reverse(buf, p); // 翻转字符串
        buffer_.add(static_cast<int>(p - buf)); // 更新 buffer 指针
    }
}

// 3. 补全各种类型的重载调用
LogStream& LogStream::operator<<(short v) {
    *this << static_cast<int>(v);
    return *this;
}

LogStream& LogStream::operator<<(unsigned short v) {
    *this << static_cast<unsigned int>(v);
    return *this;
}

LogStream& LogStream::operator<<(int v) {
    formatInteger(v);
    return *this;
}

LogStream& LogStream::operator<<(unsigned int v) {
    formatInteger(v);
    return *this;
}

LogStream& LogStream::operator<<(long v) {
    formatInteger(v);
    return *this;
}

LogStream& LogStream::operator<<(unsigned long v) {
    formatInteger(v);
    return *this;
}

LogStream& LogStream::operator<<(long long v) {
    formatInteger(v);
    return *this;
}

LogStream& LogStream::operator<<(unsigned long long v) {
    formatInteger(v);
    return *this;
}

LogStream& LogStream::operator<<(double v)
{
    if (buffer_.avail() >= kMaxNumericSize)
    {
        // 浮点数比较复杂，为了稳健，先用 snprintf
        int len = snprintf(const_cast<char*>(buffer_.current()), kMaxNumericSize, "%.12g", v);
        buffer_.add(len);
    }
    return *this;
}
```



### Logger
每次调用宏都会创建一个Logger对象，析构的时候将存储的日志信息添加上行数，文件名并刷新到队列里面

```cpp
#ifndef LOGGER_H
#define LOGGER_H

#include "LogStream.h"
#include <functional>
#include <string>
using namespace std;

// 定义日志级别，方便后期过滤
enum LogLevel 
{
    INFO,
    WARN,
    ERROR,
    FATAL,
    DEBUG
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
#define LOG_INFO Logger(__FILE__, __LINE__, INFO).stream()
#define LOG_WARN Logger(__FILE__, __LINE__, WARN).stream()
#define LOG_ERROR Logger(__FILE__, __LINE__, ERROR).stream()
#define LOG_FATAL Logger(__FILE__, __LINE__, FATAL).stream()
#define LOG_DEBUG Logger(__FILE__, __LINE__, DEBUG).stream()

#endif
```



```cpp
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
    const char* levelStr[] = { "[INFO] ", "[ERROR] ", "[FATAL] ", "[DEBUG] " };
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
```

上面最重要的是OutputFunc，决定把数据刷新到哪里

这里是添加到异步刷新的缓冲区里面，等待统一写入



<!-- 这是一张图片，ocr 内容为： -->
![](https://cdn.nlark.com/yuque/0/2026/png/54962239/1773040771687-43a2351f-63ec-4b39-ad84-52a0374ef161.png)

<!-- 这是一张图片，ocr 内容为： -->
![](https://cdn.nlark.com/yuque/0/2026/png/54962239/1773040790473-a069f693-b7bc-4a03-af53-896d84bfae09.png)



为了能够方便关闭日志，在使用宏的时候先if检查一下当前的日志级别

```cpp
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
```





### AsyncLogging
这个类很复杂，到目前我也没有消化掉，为什么采用这种方式刷新呢

每一个BufferPtr都能够存储1000条日志，最后还有一个BufferVector里面存储16个BufferPtr，根据日志的大小动态的扩容

```cpp
#ifndef ASYNC_LOGGING_H
#define ASYNC_LOGGING_H
#include "LogStream.h"
#include "noncopyable.h"
#include <vector>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <string>
#include <functional>

class AsyncLogging : noncopyable
{
public:
	// basename: 日志文件名, roll_size: 达到多大字节换新文件, flushInterval: 强制刷新间隔（秒）
	AsyncLogging(const std::string& base_name, off_t rool_size, int flush_interval = 3);

	~AsyncLogging()
	{
		if (running_)
			stop();
	}

	void append(const char* log_line, int len);							// 把数据拷贝到当前的 Buffer中
	void start()
	{
		running_ = true;
		thread_ = std::thread(bind(&AsyncLogging::threadFunc, this));
	}

	void stop()
	{
		running_ = false;
		cond_.notify_all();
		if (thread_.joinable())
		{
			thread_.join();
		}
	}

private:
	void threadFunc();													// 后端线程执行的函数，负责把内存里的数据刷新到磁盘

	// 4MB 专门用于异步缓存
	using Buffer = FixedBuffer<kLargeBuffer>;
	using BufferPtr = std::unique_ptr<Buffer>;
	using BufferVector = std::vector<BufferPtr>;

	const int flush_interval_;											// 定时刷新时间
	std::atomic<bool> running_;											// 运行状态
	std::string base_name_;												// 文件名
	off_t roll_size_;													// 文件滚动大小

	std::thread thread_;												// 后端线程
	std::mutex mutex_;													// 互斥锁
	std::condition_variable cond_;										// 条件变量，用于通知后端线程
	
	BufferPtr curr_buffer_;												// 当前正在写入的缓冲区
	BufferPtr next_buffer_;												// 下一个待写入的缓冲区
	BufferVector buffers_;												// 待写入文件的队列	
};

#endif
```



每一次的写入都是往curr_buffer里面去写，当curr满了之后，为了创建缓冲区，直接将next申请的地址转交给curr_buffer，然后通知线程赶紧把内容写入，并重新为next申请缓冲区



写入的时候，把curr中的内容追加到队列里，然后重新为这两个缓冲区分配空间，直接复制临时缓冲区的空间



一般来说最后写入的缓冲区不会大于两个，除非一次要写入的内容过于的大了，但是如果过于大，这里会直接丢弃掉的，所以永远不会出现这种情况

然后是检查临时缓冲区是否为空，如果为空，就把临时队列里的两个缓冲区拿来用，毕竟这两个还是之前的curr_和next_，所以都不需要反复的创建，一共就4个缓冲区，要写入的时候，将脏的复制到临时队列里，写完了之后重置索引就又变成新的了，理论上就是这4个缓冲区，根本不会反复的创建缓冲区，要说创建只有每一行的日志，在被反复的创建，因为每次调用LOG_INFO，都是创建了一个Logger对象



```cpp
#include "Log/AsyncLogging.h"
#include <chrono>
#include <cstdio>

AsyncLogging::AsyncLogging(const std::string& base_name, off_t rool_size, int flush_interval)
    : flush_interval_(flush_interval),
    running_(false),
    base_name_(base_name),
    roll_size_(rool_size),
    curr_buffer_(new Buffer),
    next_buffer_(new Buffer),
    buffers_()
{
    curr_buffer_->reset();
    next_buffer_->reset();
    buffers_.reserve(16);
}

void AsyncLogging::append(const char* log_line, int len)
{
    std::unique_lock<std::mutex> lock(mutex_);

    if (curr_buffer_->avail() > len)
    {
        // 当前的缓冲区足够大
        curr_buffer_->append(log_line, len);
    }
    else
    {
        // 缓冲区满了，把当前缓冲区丢到队列里
        buffers_.push_back(std::move(curr_buffer_));

        // 下一个缓冲区存在
        if (next_buffer_)
        {
            curr_buffer_ = std::move(next_buffer_);
        }
        else
        {
            curr_buffer_.reset(new Buffer);                                     // 因为BufferPtr是unique_ptr,所以要reset
        }

        curr_buffer_->append(log_line, len);
        cond_.notify_one();
    }
}

void AsyncLogging::threadFunc()
{
    // 准备两个空缓冲区
    BufferPtr new_buffer_1(new Buffer);
    BufferPtr new_buffer_2(new Buffer);
    BufferVector buffers_to_write;
    buffers_to_write.reserve(16);
    
    while (running_)
    {
        {
            // 临界区
            std::unique_lock<std::mutex> lock(mutex_);
            if (buffers_.empty())
            {
                cond_.wait_for(lock, std::chrono::seconds(flush_interval_));                        // 等待
            }

            buffers_.push_back(std::move(curr_buffer_));                                            // 清空当前缓冲区
            curr_buffer_ = std::move(new_buffer_1);                                                 // 重定义
            buffers_to_write.swap(buffers_);                                                        // 交换缓冲队列
            if (!next_buffer_)
            {
                next_buffer_ = std::move(new_buffer_2);
            }   
        }

        // 处理要写入的缓冲队列
        for (const auto& buffer : buffers_to_write)
        {
            fwrite(buffer->data(), 1, buffer->length(), stdout);
        }

        // 如果缓冲队列的大小>2,只保留两个
        if (buffers_to_write.size() > 2) { buffers_to_write.resize(2); }
        if (!new_buffer_1) 
        {
            if (!buffers_to_write.empty())
            {
                new_buffer_1 = std::move(buffers_to_write.back());
                buffers_to_write.pop_back();
                new_buffer_1->reset();
            }
            else
            {
                new_buffer_1.reset(new Buffer);
            }
        }

        if (!new_buffer_2)
        {
            if (!buffers_to_write.empty())
            {
                new_buffer_2 = std::move(buffers_to_write.back());
                buffers_to_write.pop_back();
                new_buffer_2->reset();
            }
            else
            {
                new_buffer_2.reset(new Buffer);
            }
        }

        buffers_to_write.clear();
        fflush(stdout);
    }
}
```



## 事件循环
如果我们没有用muduo，那我们肯定是在一个大循环里面，调用epoll_wait，拿到活跃的events，然后获取fd，根据每个fd的类型，判断是listen_fd，还是socket_fd，然后判断是读事件还是写事件



最开始的代码可能是这样子的

```cpp
while(true) {
    int num = epoll_wait(epollfd, events, ...);
    for(int i=0; i<num; ++i) {
        int fd = events[i].data.fd;
        if (fd == listenfd) { 
            // 处理 accept
        } else if (events[i].events & EPOLLIN) {
            // 处理 read
        } else if (events[i].events & EPOLLOUT) {
            // 处理 write
        }
    }
}
```



但是每个fd的读事件所要触发的回调又不尽一样，同理写事件也是如此，而且如果我们想往epoll添加一个监听事件，是不是就需要自己写好几个函数，比如添加socketfd，添加wakefd等，所以我们直接为每个fd维护一个读写回调以及添加，修改，删除和监听读写事件的类，这样我们只需要创建这个类，然后为读写设置回调，因为每种fd的回调都不一样的，其次选择其是监听读事件还是写事件在将fd注册到epoll里，这就是channel的由来



但是为什么要设计一个poller，为什么不直接在eventloop里面直接把那个大循环跑起来，毕竟我们已经把循环里面的if，都拆成了channel了



对于eventloop，我们的设计是其在有事件触发的fd，统一调用对应事件的回调函数，然后把别的线程投递到任务队列里的任务执行一遍

所以我们需要关注哪个fd触发的事件吗？不需要，只需要知道每次epoll唤醒之后，活跃的fd是哪些就行了



这是eventloop的职责，把大循环都拆分出去了，只关注触发的事件，并执行线程投递的任务

<!-- 这是一张图片，ocr 内容为： -->
![](https://cdn.nlark.com/yuque/0/2026/png/54962239/1773111843469-61eb2d4a-3741-48ac-970f-4264ae20afc1.png)



但是我们也需要管理epoll上有几个fd，不然事件触发之后，我们怎么根据这个fd把对应的channel传出去？因为eventloop只需要获取触发事件的channel数组即可

这就是poller的由来，负责管理epoll中每个channel的状态，以及监听他们是否触发，把所有触发的channel全都交给eventloop来统一执行事件回调



所以我们目前需要设计的类，一个虚基类Poller，一个EpollPoller，还有一个EvnetLoop以及管理fd的Channel







### Poller
基于上述的思考，我们需要一个channel数组，一个负责储存返回触发事件的数组，以及一些更新fd在epoll的状态和删除fd的功能



代码如下，保存一个fd和Channel的映射

```cpp
#ifndef POLLER_H
#define POLLER_H

#include "noncopyable.h"
#include <vector>
#include <map>

class Channel;
class EventLoop;

class Poller : noncopyable
{
public:
	using ChannelList = std::vector<Channel*>;													// 用于返回发生事件的连接

	Poller(EventLoop* loop);
	virtual ~Poller();

	static Poller* NewDefaultPoller(EventLoop* loop);											// 静态工厂方法

public:
	virtual void Poll(int timeoutMs, ChannelList* active_channels) = 0;							// 必须在EventLoop所在的线程中调用，查询事件是否触发
	virtual void UpdateChannel(Channel* channel) = 0;											// 更新Channel，把fd加入epoll，或者修改状态
	virtual void RemoveChannel(Channel* channel) = 0;											// 移除Channel，把fd从epoll中移除

	// 判断Channel是否在当前的Poller中
    bool HasChannel(Channel* channel) const;

protected:
	using ChannelMap = std::map<int, Channel*>;													// 用于记录fd和Channel的对应关系
	ChannelMap channels_;

private:
	EventLoop* owner_loop_;
};

#endif
```



```cpp
#include "net/Poller.h"
#include "net/Channel.h"
#include "net/EPollPoller.h"

Poller::Poller(EventLoop* loop)
	:owner_loop_(loop)
{

}

Poller::~Poller()
{

}

Poller* Poller::NewDefaultPoller(EventLoop* loop)
{
	return new EPollPoller(loop);
}

bool Poller::HasChannel(Channel* channel) const
{
	auto it = channels_.find(channel->fd());
	return it != channels_.end() && it->second == channel;
}
```



### EPollPoller
重载基类里的函数

#### Poll
职责：关注触发的fd，并通知channel触发的是什么事件，把所有的channel转交给eventloop

```cpp
void EPollPoller::Poll(int timeoutMs, ChannelList* active_channels)
{
	int num_events = ::epoll_wait(epoll_fd_, &*events_.begin(), static_cast<int>(events_.size()), timeoutMs);
	int save_err = errno;

	if (num_events > 0)
	{
		FillActiveChannels(num_events, active_channels);

		// 如果一轮下来16个位置都满了，自动扩容
		if (num_events == static_cast<int>(events_.size()))
		{
			events_.resize(events_.size() * 2);
		}
	}
	else if (num_events == 0)
	{
		// 超时
	}
	else
	{
		// 报错，如果不是信号中断，打印错误日志
		if (save_err != EINTR)
		{
			save_err = errno;
            LOG_ERROR << "epoll_wait error";
		}
	}
}

// 把内核返回的epoll_event 转换成 Channel
void EPollPoller::FillActiveChannels(int num_evnets, ChannelList* active_channels) const
{
	for (int i = 0; i < num_evnets; ++i)
	{
		Channel* channel = static_cast<Channel*>(events_[i].data.ptr);
		channel->set_revents(events_[i].events);												// 告诉Channel发生了什么事件
		active_channels->push_back(channel);													// EveentLoop会调用Channel的HandleEvent
	}
}
```



#### UpdateChannel
当Channel监听读事件，或者监听写事件亦或者什么都不监听，Channel都是通过loop将这个监听结果交给UpdateChannel进行处理

<!-- 这是一张图片，ocr 内容为： -->
![](https://cdn.nlark.com/yuque/0/2026/png/54962239/1773112481231-fd731d34-0d8a-4a35-a536-f12084d11e52.png)

<!-- 这是一张图片，ocr 内容为： -->
![](https://cdn.nlark.com/yuque/0/2026/png/54962239/1773112496126-f51d783d-4aee-49db-8259-c33cccdf338b.png)

<!-- 这是一张图片，ocr 内容为： -->
![](https://cdn.nlark.com/yuque/0/2026/png/54962239/1773112560356-943e85a7-93be-4607-8f1e-3ee6fc73742d.png)



在每个Channel中，记录一个当前在epoll中的状态

```cpp
// Channel的状态标识
const int kNew = 1;											// 未添加进epoll
const int kAdded = 2;										// 已添加进epoll
const int kDeleted = 3;										// 已从epoll中删除
```



根据这些状态进行对应的在epoll中的操作

值得一提的是为什么我们不监听任何事件也是用UpdateChannel为什么不是Remove呢？

因为我们的映射保留的是指针，不是shared_ptr，remove会直接将Channel删除掉，但是我们的本意是不想监听任何事件，只是不想它在epoll中占据资源了，但并不想删除Channel

所以检测到不监听任何事件会直接从epoll将该fd删除掉，但是依旧保留映射，只有fd要销毁，比如连接断开时，才进行映射的移除；

```cpp
using ChannelMap = std::map<int, Channel*>;
ChannelMap channels_;
```



```cpp
void EPollPoller::UpdateChannel(Channel* channel)
{
	const int index = channel->index();															// index 在这里被复用来储存状态
	const int fd = channel->fd();
	if (index == kNew || index == kDeleted)
	{
		// 第一次加入
		if (index == kNew)
		{
			assert(channels_.find(fd) == channels_.end());
			channels_[channel->fd()] = channel;
		}

		// 为避免占用红黑树资源的暂时关闭
		if (index == kDeleted)
		{
			assert(channels_.find(fd) != channels_.end());
			assert(channels_[fd] == channel);
		}

		channel->set_index(kAdded);
		Update(EPOLL_CTL_ADD, channel);
	}
	else
	{
		assert(channels_.find(fd) != channels_.end());
		assert(channels_[fd] == channel);
		// 如果不监听任何事件，避免资源浪费，从epoll中删除，但是不关闭映射
		if (channel->IsNoneEvent())
		{
			Update(EPOLL_CTL_DEL, channel);
            channel->set_index(kDeleted);
		}
		else
		{
			// 已经在 epoll 里了：使用 MOD
			Update(EPOLL_CTL_MOD, channel);
		}
	}
}
```



#### RemoveChannel
进行映射的移除

```cpp
void EPollPoller::RemoveChannel(Channel* channel)
{
	int fd = channel->fd();
	channels_.erase(fd);

	int index = channel->index();
	if (index == kAdded)
	{
		Update(EPOLL_CTL_DEL, channel);
	}

	channel->set_index(kNew);
}
```



#### 完整代码
```cpp
#ifndef EPOLL_POLLER_H
#define EPOLL_POLLER_H

#include "Poller.h"
#include <vector>
#include <sys/epoll.h>

class EPollPoller : public Poller
{
public:
	EPollPoller(EventLoop* loop);
	~EPollPoller() override;

public:
	virtual void Poll(int timeoutMs, ChannelList* active_channels);
	virtual void UpdateChannel(Channel* channel);											
	virtual void RemoveChannel(Channel* channel);											

private:
	// 初始状态下，epoll监听列表的大小
	static const int k_init_event_list_size = 16;

	void FillActiveChannels(int num_evnets, ChannelList* active_channels) const;				// 填充活跃的链接
	void Update(int operation, Channel* channel);												// 更新epoll状态(epoll_ctl)

private:
	using EventList = std::vector<struct epoll_event>;
	int epoll_fd_;
	EventList events_;																			// 用于接收内核返回的事件数组
};
#endif
```



```cpp
#include "net/EPollPoller.h"
#include "Log/Logger.h"
#include "net/Channel.h"
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <cassert>

// Channel的状态标识
const int kNew = 1;											// 未添加进epoll
const int kAdded = 2;										// 已添加进epoll
const int kDeleted = 3;										// 已从epoll中删除


EPollPoller::EPollPoller(EventLoop* loop)
	: Poller(loop),
	epoll_fd_(::epoll_create1(EPOLL_CLOEXEC)),
	events_(k_init_event_list_size)
{
	if (epoll_fd_ < 0)
	{
		LOG_FATAL << "epoll_create1 error";
	}
}

EPollPoller::~EPollPoller()
{
	::close(epoll_fd_);
}

void EPollPoller::Poll(int timeoutMs, ChannelList* active_channels)
{
	int num_events = ::epoll_wait(epoll_fd_, &*events_.begin(), static_cast<int>(events_.size()), timeoutMs);
	int save_err = errno;

	if (num_events > 0)
	{
		FillActiveChannels(num_events, active_channels);

		// 如果一轮下来16个位置都满了，自动扩容
		if (num_events == static_cast<int>(events_.size()))
		{
			events_.resize(events_.size() * 2);
		}
	}
	else if (num_events == 0)
	{
		// 超时
	}
	else
	{
		// 报错，如果不是信号中断，打印错误日志
		if (save_err != EINTR)
		{
			save_err = errno;
            LOG_ERROR << "epoll_wait error";
		}
	}
}

void EPollPoller::UpdateChannel(Channel* channel)
{
	const int index = channel->index();															// index 在这里被复用来储存状态
	const int fd = channel->fd();
	if (index == kNew || index == kDeleted)
	{
		// 第一次加入
		if (index == kNew)
		{
			assert(channels_.find(fd) == channels_.end());
			channels_[channel->fd()] = channel;
		}

		// 为避免占用红黑树资源的暂时关闭
		if (index == kDeleted)
		{
			assert(channels_.find(fd) != channels_.end());
			assert(channels_[fd] == channel);
		}

		channel->set_index(kAdded);
		Update(EPOLL_CTL_ADD, channel);
	}
	else
	{
		assert(channels_.find(fd) != channels_.end());
		assert(channels_[fd] == channel);
		// 如果不监听任何事件，避免资源浪费，从epoll中删除，但是不关闭映射
		if (channel->IsNoneEvent())
		{
			Update(EPOLL_CTL_DEL, channel);
            channel->set_index(kDeleted);
		}
		else
		{
			// 已经在 epoll 里了：使用 MOD
			Update(EPOLL_CTL_MOD, channel);
		}
	}
}

void EPollPoller::RemoveChannel(Channel* channel)
{
	int fd = channel->fd();
	channels_.erase(fd);

	int index = channel->index();
	if (index == kAdded)
	{
		Update(EPOLL_CTL_DEL, channel);
	}

	channel->set_index(kNew);
}

// 把内核返回的epoll_event 转换成 Channel
void EPollPoller::FillActiveChannels(int num_evnets, ChannelList* active_channels) const
{
	for (int i = 0; i < num_evnets; ++i)
	{
		Channel* channel = static_cast<Channel*>(events_[i].data.ptr);
		channel->set_revents(events_[i].events);												// 告诉Channel发生了什么事件
		active_channels->push_back(channel);													// EveentLoop会调用Channel的HandleEvent
	}
}

void EPollPoller::Update(int operation, Channel* channel)
{
	struct epoll_event event;
	memset(&event, 0, sizeof(event));

	event.events = channel->events();
	event.data.ptr = channel;

	int fd = channel->fd();
	if (::epoll_ctl(epoll_fd_, operation, fd, &event) < 0)
	{
		if (operation == EPOLL_CTL_DEL)
		{
			LOG_ERROR << "epoll_ctl del error";
		}
		else
		{
			LOG_FATAL << "epoll_ctl add/mod error";
		}
	}
}
```



### Channel
fd的配套设施，fd事件触发的回调，fd在epoll中的状态，fd在epoll中监听事件类别



#### 事件触发的回调
先设置四种事件对应的回调

```cpp
void SetReadCallback(ReadEventCallback cb) { read_callback_ = std::move(cb); }
void SetWriteCallback(EventCallback cb) { write_callback_ = std::move(cb); }
void SetCloseCallback(EventCallback cb) { close_callback_ = std::move(cb); }
void SetErrorCallback(EventCallback cb) { error_callback_ = std::move(cb); }
```



记录监听事件的状态

<!-- 这是一张图片，ocr 内容为： -->
![](https://cdn.nlark.com/yuque/0/2026/png/54962239/1773114935306-e39a07ec-84a1-487c-9e31-0ea71c9868b8.png)



封装统一的回调接口，供eventloop调用，根据实际发生的事件来调用对应的回调，因为一个fd可能监听了多个事件，但是只会有一个事件被触发

这里值得一体的是，为了确保在执行回调期间，其上层持有channel的类生命周期是有效的，会持有一个weakptr

```cpp
// 确保对象生命周期安全
std::weak_ptr<void> tie_;
// 绑定对象，确保对象生命周期安全
void tie(const std::shared_ptr<void>& obj) { tie_ = obj; btied_ = true; }
```



```cpp
// 处理事件
void HandleEvent();
void HandleEventWithGuard();

void Channel::HandleEvent()
{
    if (btied_)
    {
        std::shared_ptr<void> gurd = tie_.lock();
        if (gurd)
        {
            HandleEventWithGuard();
        }
    }
    else
    {
        HandleEventWithGuard();
    }
}

void Channel::HandleEventWithGuard()
{
    // 1. 发生错误
    if (revents_ & EPOLLERR)
    {
        if (error_callback_) error_callback_();
    }

    // 2. 对端关闭连接
    if ((revents_ & EPOLLHUP) && !(revents_ & EPOLLIN))
    {
        if (close_callback_) close_callback_();
    }

    // 3. 可读事件(紧急数据)
    if (revents_ & (EPOLLIN | EPOLLPRI | EPOLLRDHUP))
    {
        if (read_callback_) read_callback_();
    }

    // 4. 可写事件
    if (revents_ & EPOLLOUT)
    {
        if (write_callback_) write_callback_();
    }
}
```



#### 注册监听事件类别
一直传递给上层的poller

```cpp
// 开启/关闭各类事件监听
void EnableReading() { events_ |= kReadEvent; Update(); }
void DisableReading() { events_ &= ~kReadEvent; Update(); }
void EnableWriting() { events_ |= kWriteEvent; Update(); }
void DisableWriting() { events_ &= ~kWriteEvent; Update(); }
void DisableAll() { events_ = kNoneEvent; Update(); }		
// 当fd所在的EventLoop想要改变监听事件时调用
void Channel::Update()
{
    loop_->UpdateChannel(this);
}
void EventLoop::UpdateChannel(Channel* channel)
{
	AssertInLoopThread();
	poller_->UpdateChannel(channel);
}
```



#### 记录在epoll中的状态
```cpp
// Channel的状态标识
const int kNew = 1;									// 未添加进epoll
const int kAdded = 2;								// 已添加进epoll
const int kDeleted = 3;								// 已从epoll中删除

// index 记录在 Poller 中的状态
int index() { return index_; }
void set_index(int idx) { index_ = idx; }
```



#### 完整代码
```cpp
#ifndef CHANNEL_H
#define CHANNEL_H

#include "noncopyable.h"
#include <functional>
#include <memory>

class EventLoop;

class Channel : noncopyable
{
public:
	using EventCallback = std::function<void()>;
	using ReadEventCallback = std::function<void()>;

	Channel(EventLoop* loop, int fd);
	~Channel();

	// 处理事件
	void HandleEvent();
	void HandleEventWithGuard();

public:
	// 设置回调函数
	void SetReadCallback(ReadEventCallback cb) { read_callback_ = std::move(cb); }
	void SetWriteCallback(EventCallback cb) { write_callback_ = std::move(cb); }
	void SetCloseCallback(EventCallback cb) { close_callback_ = std::move(cb); }
	void SetErrorCallback(EventCallback cb) { error_callback_ = std::move(cb); }

	int fd() const { return fd_; }
	int events() const { return events_; }
	void set_revents(int revt) { revents_ = revt; }												// 由 Poller 设置，告诉内核实际发生了什么

	// 判断当前是否监听了任何事件
	bool IsNoneEvent() const { return events_ == kNoneEvent; }
	bool IsWriting() const { return events_ & kWriteEvent; }
    bool IsReading() const { return events_ & kReadEvent; }

	// 开启/关闭各类事件监听
	void EnableReading() { events_ |= kReadEvent; Update(); }
	void DisableReading() { events_ &= ~kReadEvent; Update(); }
	void EnableWriting() { events_ |= kWriteEvent; Update(); }
	void DisableWriting() { events_ &= ~kWriteEvent; Update(); }
	void DisableAll() { events_ = kNoneEvent; Update(); }										// 暂时关闭，触发Del从红黑树上删除，但是依旧保留映射

	// index 记录在 Poller 中的状态
	int index() { return index_; }
	void set_index(int idx) { index_ = idx; }

	// 返回所属的 EventLoop
	EventLoop* ownerLoop() { return loop_; }
	void Remove();																				// 删除映射关系

	// 绑定对象，确保对象生命周期安全
    void tie(const std::shared_ptr<void>& obj) { tie_ = obj; btied_ = true; }

private:
	void Update();

private:
	static const int kNoneEvent;
	static const int kReadEvent;
	static const int kWriteEvent;

	EventLoop* loop_;								// 所属的事件循环
	const int fd_;									// 封装的fd
	int events_;									// 注册的感兴趣的事件
	int revents_;									// 实际发生的事件					
	int index_;										// 在poller中的状态(未添加，已添加，删除)

	// 确保对象生命周期安全
	std::weak_ptr<void> tie_;
	bool btied_;									// 是否绑定了对象

	ReadEventCallback read_callback_;
	EventCallback write_callback_;
	EventCallback close_callback_;
	EventCallback error_callback_;
};

#endif
```



```cpp
#include "net/Channel.h"
#include "net/EventLoop.h"
#include <sys/epoll.h>

const int Channel::kNoneEvent = 0;
const int Channel::kReadEvent = EPOLLIN | EPOLLPRI;
const int Channel::kWriteEvent = EPOLLOUT;

Channel::Channel(EventLoop* loop, int fd)
    : loop_(loop),
    fd_(fd),
    events_(0),
    revents_(0),
    index_(1),
    btied_(false)
{
	
}

Channel::~Channel()
{

}

void Channel::Remove()
{
    loop_->RemoveChannel(this);
}

// 当fd所在的EventLoop想要改变监听事件时调用
void Channel::Update()
{
    loop_->UpdateChannel(this);
}

void Channel::HandleEvent()
{
    if (btied_)
    {
        std::shared_ptr<void> gurd = tie_.lock();
        if (gurd)
        {
            HandleEventWithGuard();
        }
    }
    else
    {
        HandleEventWithGuard();
    }
}

void Channel::HandleEventWithGuard()
{
    // 1. 发生错误
    if (revents_ & EPOLLERR)
    {
        if (error_callback_) error_callback_();
    }

    // 2. 对端关闭连接
    if ((revents_ & EPOLLHUP) && !(revents_ & EPOLLIN))
    {
        if (close_callback_) close_callback_();
    }

    // 3. 可读事件(紧急数据)
    if (revents_ & (EPOLLIN | EPOLLPRI | EPOLLRDHUP))
    {
        if (read_callback_) read_callback_();
    }

    // 4. 可写事件
    if (revents_ & EPOLLOUT)
    {
        if (write_callback_) write_callback_();
    }
}

```



### EventLoop
只做两件事，执行所以事件触发的回调，执行别的线程投递到本线程的任务

#### Loop
先执行所有事件触发的回调在执行别的线程投递到本线程的任务

```cpp
void EventLoop::Loop()
{
	looping_ = true;
	quit_ = false;

	while (!quit_)
	{
		active_channels_.clear();

		poller_->Poll(10000, &active_channels_);
		for (Channel* channel : active_channels_)
		{
			channel->HandleEvent();
		}
        DoPendingFunctors();														// 每一轮循环后，处理跨线程投递的任务
	}

	looping_ = false;
}
```



#### 跨线程投递
跨线程是有wakefd来往epoll里写事件，触发wakefd的读事件，来唤醒epoll，这样就会执行投递过来的任务了



```cpp
int wakeup_fd_;                                                 // 唤醒fd
std::unique_ptr<Channel> wake_channel_;                         // 唤醒channel
wake_channel_->SetReadCallback(bind(&EventLoop::HandleRead, this));
wake_channel_->EnableReading();												// 监听唤醒事件
void EventLoop::WakeUp()
{
	uint64_t one = 1;
	::write(wakeup_fd_, &one, sizeof one);								// 只要将wakefd注册到epoll中，这里主动写数据等于触发了该fd的读事件
}

void EventLoop::HandleRead()
{
	uint64_t one = 1;
	::read(wakeup_fd_, &one, sizeof one);
}
```



```cpp
void EventLoop::RunInLoop(Functor cb)
{
	if (IsInLoopThread())
	{
		cb();
	}
	else
	{
		QueueInLoop(std::move(cb));
	}
}

void EventLoop::QueueInLoop(Functor cb)
{
	{
		std::lock_guard<std::mutex> lock(mutex_);
		pending_functors_.push_back(std::move(cb));
	}

	/*
	* 读事件是可以保留到下一次调用epoll_wait的
	* 所以如果此时刚好处于处理任务的阶段，为避免当前的任务被延迟到下一个超时事件才唤醒
	* 如果换成if(!calling_pending_functors_) 此时没有执行任务，直接添加到任务队列就好了呀，就能到这一个轮次里了，没必要唤醒
	* if(calling_pending_functors_) 只是为了将任务添加到下一个轮次，当前轮次的任务执行完之后，立即触发读事件，接着下一轮次的任务处理，这样形成的无缝衔接
	* 目的：确保它处理完当前任务后，能立即从下一轮 poll 中跳出来处理新加入的任务
	* 
	* 如果不在本线程上也是需要唤醒的，避免任务延迟
	*/
	if (!IsInLoopThread() || calling_pending_functors_)
	{
		WakeUp();
	}
}
```

#### 
#### 完整代码
```cpp
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
```



```cpp
#include "net/EventLoop.h"
#include "Log/Logger.h"
#include "net/Poller.h"
#include <cassert>
#include <sys/eventfd.h>
#include <unistd.h>

int CreateEventFD()
{
	int event_fd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
	if (event_fd < 0) abort();
	return event_fd;
}

// 线程局部存储, 确保每个线程只有一个 EventLoop
__thread EventLoop* t_loopInThisThread = nullptr;

EventLoop::EventLoop()
	:thread_id_(std::this_thread::get_id()),
	poller_(Poller::NewDefaultPoller(this)),
	wakeup_fd_(CreateEventFD()),
	wake_channel_(new Channel(this, wakeup_fd_)),
	timer_queue_(new TimerQueue(this))
{
	if (t_loopInThisThread)
	{
		LOG_FATAL << "Another EventLoop exits in this thread";
	}
	else
	{
		t_loopInThisThread = this;
		wake_channel_->SetReadCallback(bind(&EventLoop::HandleRead, this));
		wake_channel_->EnableReading();												// 监听唤醒事件									
	}
}

void EventLoop::RunInLoop(Functor cb)
{
	if (IsInLoopThread())
	{
		cb();
	}
	else
	{
		QueueInLoop(std::move(cb));
	}
}

void EventLoop::QueueInLoop(Functor cb)
{
	{
		std::lock_guard<std::mutex> lock(mutex_);
		pending_functors_.push_back(std::move(cb));
	}

	/*
	* 读事件是可以保留到下一次调用epoll_wait的
	* 所以如果此时刚好处于处理任务的阶段，为避免当前的任务被延迟到下一个超时事件才唤醒
	* 如果换成if(!calling_pending_functors_) 此时没有执行任务，直接添加到任务队列就好了呀，就能到这一个轮次里了，没必要唤醒
	* if(calling_pending_functors_) 只是为了将任务添加到下一个轮次，当前轮次的任务执行完之后，立即触发读事件，接着下一轮次的任务处理，这样形成的无缝衔接
	* 目的：确保它处理完当前任务后，能立即从下一轮 poll 中跳出来处理新加入的任务
	* 
	* 如果不在本线程上也是需要唤醒的，避免任务延迟
	*/
	if (!IsInLoopThread() || calling_pending_functors_)
	{
		WakeUp();
	}
}

void EventLoop::DoPendingFunctors()
{
	std::vector<Functor> tasks;
	calling_pending_functors_ = true;

	{
        std::lock_guard<std::mutex> lock(mutex_);
        tasks.swap(pending_functors_);
	}

	for (const Functor& task : tasks)
	{
		task();
	}
	calling_pending_functors_ = false;
}

void EventLoop::WakeUp()
{
	uint64_t one = 1;
	::write(wakeup_fd_, &one, sizeof one);								// 只要将wakefd注册到epoll中，这里主动写数据等于触发了该fd的读事件
}

void EventLoop::HandleRead()
{
	uint64_t one = 1;
	::read(wakeup_fd_, &one, sizeof one);
}

void EventLoop::Loop()
{
	looping_ = true;
	quit_ = false;

	while (!quit_)
	{
		active_channels_.clear();

		poller_->Poll(10000, &active_channels_);
		for (Channel* channel : active_channels_)
		{
			channel->HandleEvent();
		}
        DoPendingFunctors();														// 每一轮循环后，处理跨线程投递的任务
	}

	looping_ = false;
}

void EventLoop::UpdateChannel(Channel* channel)
{
	AssertInLoopThread();
	poller_->UpdateChannel(channel);
}

void EventLoop::RemoveChannel(Channel* channel)
{
	AssertInLoopThread();
	poller_->RemoveChannel(channel);
}

void EventLoop::Quit() 
{
	quit_ = true;
	// 如果你在 Loop 里面阻塞在 poll() 上，这里需要一个 wakeup 机制把它叫醒
	// wakeup(); 
}

// 检查 Channel 是否归属当前 Poller
bool EventLoop::HasChannel(Channel* channel) 
{
	return poller_->HasChannel(channel);
}

// 定时器
void EventLoop::RunAfter(double delay, Timer::TimerCallback cb)
{
	Timestamp time(AddTime(Timestamp::now(), delay));
	timer_queue_->AddTimer(std::move(cb), time, 0);
}

void EventLoop::RunEvery(double interval, Timer::TimerCallback cb)
{
	Timestamp time(AddTime(Timestamp::now(), interval));
	timer_queue_->AddTimer(std::move(cb), time, interval);
}

void EventLoop::AbortNotInLoopThread()
{
	LOG_FATAL << "EventLoop::abortNotInLoopThread - EventLoop was created in threadId_ = "
		<< thread_id_
		<< ", current thread id = "
		<< std::this_thread::get_id();
}

EventLoop::~EventLoop()
{
	t_loopInThisThread = nullptr;
}

```



### 关于线程安全
muduo的设计很巧妙，不需要像qt那样去手动的movetothread，而且也不需要管理对象究竟是在哪里创建的

不过所有的对象的构造函数都要包含一个loop指针，当调用该对象下的API时，采用的是先获取Loop指针，然后将要调用的函数用bind绑定一下参数，交给这个loop的RunInLoop执行

这就需要开发者需要很清晰的框架认知，而且对多线程的运行以及对象的被控制权和线程归属有很强的理解



所以我们引入一个ThreadSwitcher类，来把先获取Loop指针，然后将要调用的函数用bind绑定一下参数，交给这个loop的RunInLoop执行这一步封装起来，像这样进行调用即可

```cpp
// 自动判断并切线程
ThreadSwitcher::run(loop, TargetObject, &Target::Func, arg1, arg2);
```



这里需要显示的传loop，为什么不引用一个基类呢？引入一个基类后续就要引入更多的代码

不如这样显而易见

虽然这样还是需要开发者对Reactor有深刻的理解，但是不必思考跨线程中，哪个函数应该在哪里执行，你可以无脑的去run就行了

```cpp
#ifndef THREAD_SWITCHER_H
#define THREAD_SWITCHER_H
#include "net/EventLoop.h"

class ThreadSwitcher
{
public:
    template<typename T, typename Method, typename... Args>
    static void Run(EventLoop* loop, T* obj, Method method, Args&&... args)
    {
        if (loop->IsInLoopThread())
        {
            (obj->*method)(std::forward<Args>(args)...);
        }
        else 
        {
            loop->QueueInLoop([=]()
                {
                    (obj->*method)(args...);
                });
        }
    }
};

#endif
```



为什么不用线程局部变量呢，因为有的时候对象不是在这个loop线程中创建的，所以你用线程获取的时候就必须要确保该对象必须在这个线程里创建，这就又要引入更多的补丁，会更加麻烦



#### Bug
使用wrk -t4 -c1000 -d30s [http://127.0.0.1:8888/](http://127.0.0.1:8888/) 进行压测时，大批量连接统一退出

<!-- 这是一张图片，ocr 内容为： -->
![](https://cdn.nlark.com/yuque/0/2026/png/54962239/1773126770283-8bf11530-5aa4-4976-8b87-eaef64a07475.png)

<!-- 这是一张图片，ocr 内容为： -->
![](https://cdn.nlark.com/yuque/0/2026/png/54962239/1773126785383-3d81291a-6270-4a66-89b2-2f49c5372ddd.png)

<!-- 这是一张图片，ocr 内容为： -->
![](https://cdn.nlark.com/yuque/0/2026/png/54962239/1773126812173-a550d9bc-86ed-4f8b-b402-dcd7c74ddd64.png)

<!-- 这是一张图片，ocr 内容为： -->
![](https://cdn.nlark.com/yuque/0/2026/png/54962239/1773126832850-ca8f1dce-83bb-42ab-ab31-b377b5a0c944.png)



程序不会因为连接的退出而销毁

AI分析是vector的析构过程中，释放其内部持有的std::function对象时，触发了堆内存管理器的崩溃

<!-- 这是一张图片，ocr 内容为： -->
![](https://cdn.nlark.com/yuque/0/2026/png/54962239/1773126918614-05dc50b8-619d-466b-ae97-392444cbb48b.png)



添加ASan，来定位是谁导致的

```cpp
add_compile_options(-fsanitize=address -g)
add_link_options(-fsanitize=address)
```



最新日志

<!-- 这是一张图片，ocr 内容为： -->
![](https://cdn.nlark.com/yuque/0/2026/png/54962239/1773127219781-8ee1c21f-33e1-4c2e-be0e-bd1840e66a01.png)

<!-- 这是一张图片，ocr 内容为： -->
![](https://cdn.nlark.com/yuque/0/2026/png/54962239/1773127179981-7369d8e4-1b3e-4503-bf2a-762e5519e3af.png)



也就是这个channel没有成功的从epoll上移除掉，追溯本源发现是ConnectDestroyed没有执行，也就是ThreadSwitcher::Run(conn->GetLoop(), conn, &TcpConnection::ConnectDestroyed);这个函数没有被执行，这里就能看出问题了

因为我是用weakptr接收的sharedptr，但是此时size_t n = connections_.erase(conn->fd());直接将引用计数➖1了，如果还用weakptr，就意味着当这个函数跑完之后，引用计数会被彻底的清空，所以修改我们的ThreadSwitcher



<!-- 这是一张图片，ocr 内容为： -->
![](https://cdn.nlark.com/yuque/0/2026/png/54962239/1773131539008-0f49befc-788a-4819-897e-ba2eef3451e6.png)



#### 完整代码
```cpp
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
};

#endif
```



## 网络
核心就两个一个是server一个是conn，server里面要做的事情就是管理所有的conn，并为每一个conn设置连接回调和消息回调以及连接关闭的回调



但是会有三个阶段，分别为单线程阶段，多线程阶段，以及连接超时管理阶段

但是我们这里从以下角度进行分析，从连接到来，到连接关闭这个闭环的逻辑

后面的多线程和连接超时会重新搞一个模块



### TcpServer
Server也需要一个套接字，就是我们上述在大循环里面提到的Listenfd，专门监听连接

为此我们创建了Acceptor，其是对Channel的在一层封装



#### Acceptor
当连接到来时，获取connfd，并将这个fd转交给连接回调

完整代码如下

```cpp
#ifndef ACCEPTOR_H
#define ACCEPTOR_H

#include "net/Channel.h"
#include "net/Socket.h"									// 你需要实现一个简易的 Socket 类封装 fd
#include <functional>

class EventLoop;

class Acceptor : noncopyable
{
public:
	using NewConnectionCallback = std::function<void(int socket_fd, const std::string& peer_addr)>;

	Acceptor(EventLoop* loop, const std::string& ip, uint16_t port);
	~Acceptor();

	void SetNewConnectionCallback(NewConnectionCallback cb) { new_connection_callback_ = std::move(cb); };
	void Listen();										// 开始监听端口									
	bool listening() const { return listening_; }

private:
	void HandleRead();								

private:
	EventLoop* loop_;
	Socket accept_socket_;
	Channel accept_channel_;
	NewConnectionCallback new_connection_callback_;
	bool listening_;
};

#endif
```



```cpp
#include "net/Acceptor.h"
#include "Log/Logger.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

Acceptor::Acceptor(EventLoop* loop, const std::string& ip, uint16_t port)
	:loop_(loop),
	accept_socket_(::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0)),
	accept_channel_(loop, accept_socket_.fd()),
	listening_(false)
{
    accept_socket_.BindAddress(ip, port);
	accept_channel_.SetReadCallback(bind(&Acceptor::HandleRead, this));
}

Acceptor::~Acceptor()
{

}

void Acceptor::Listen()
{
	listening_ = true;
	accept_socket_.Listen();
	accept_channel_.EnableReading();																	// 把 listen fd 加入 epoll
}

void Acceptor::HandleRead() 
{
    int connfd = accept_socket_.Accept(); // accept 新连接
    if (connfd >= 0) 
    {
        if (new_connection_callback_) 
        {
            // 将新 fd 抛给上层业务
            new_connection_callback_(connfd, "peer_address");
        }
        else
        {
            ::close(connfd);
        }
    }
    else
    {
        LOG_ERROR << "Acceptor::HandleRead error";
    }
}
```



#### 启动服务
根据传参来决定是否需要开启多线程

```cpp
void TcpServer::Start(int thread_num) 
{
    thread_pool_->SetThreadNum(thread_num);
    thread_pool_->Start();
    acceptor_->Listen();

    // 开始时间轮
    loop_->RunEvery(1.0, [this]() 
        {
        this->Tick(); // 时间轮，监测所有连接的超时
        });
}
```



#### 当新连接到来时
根据是否开启了线程池来获取对应的loop，然后创建连接对象

其次是为连接设置消息回调，来处理每条消息，以及连接和关闭回调

最后放到map中进行管理，并将conn注册到自己的事件循环中

```cpp
void TcpServer::NewConnection(int sockfd, const std::string& peerAddr) 
{
    LOG_INFO << "TcpServer::NewConnection - new connection from " << peerAddr << "sockfd = " << sockfd;

    // 不再使用主线程的loop_
    EventLoop* io_loop = thread_pool_->GetNextLoop();
    EventLoop* loop = (io_loop != nullptr) ? io_loop : loop_;

    // 1. 创建 TcpConnection 对象
    auto conn = std::make_shared<TcpConnection>(loop, sockfd);

    // 2. 挪移到时间轮中管理超时
    TcpConnection::EntryPtr entry = std::make_shared<TcpConnection::Entry>(conn);
    conn->SetEntry(entry);
    RefreshEntry(entry);

    std::weak_ptr<TcpConnection::Entry> weak_entry = entry;

    // 3. 为连接设置消息回调
    auto wrapped_message_cb = [this, weak_entry](const TcpConnectionPtr& conn, Buffer* buf)
        {
            // 1. 续命：将 Entry 移到时间轮的最新桶
            auto entry = weak_entry.lock();
            if (entry)
            {
                RefreshEntry(entry);
            }

            // 2. 执行用户原始回调
            if (this->message_cb_) 
            {
                this->message_cb_(conn, buf);
            }
        };

    conn->SetMessageCallback(wrapped_message_cb);
    conn->SetConnectionCallback(connection_callback_);
    conn->SetCloseCallback(std::bind(&TcpServer::RemoveConnection, this, std::placeholders::_1));

    // 4. 将连接加入 map 管理
    connections_[sockfd] = std::move(conn);

    // 5. 向Epoll注册连接
    ThreadSwitcher::Run(loop, connections_[sockfd], &TcpConnection::ConnectEstablished);
}
```



#### 连接关闭时
最后从map中将连接释放掉

```cpp
void TcpServer::RemoveConnection(const std::shared_ptr<TcpConnection>& conn)
{
    ThreadSwitcher::Run(loop_, this, &TcpServer::RemoveConnectionInLoop, conn);
}

void TcpServer::RemoveConnectionInLoop(const std::shared_ptr<TcpConnection>& conn)
{
    loop_->AssertInLoopThread();

    // 1. 将最后的销毁动作放入队列
    // 注意：这里 bind 再次持有了 conn 的副本，引用计数+1，保证了 ConnectDestroyed 执行时对象不被析构
    ThreadSwitcher::Run(conn->GetLoop(), conn, &TcpConnection::ConnectDestroyed);

    // 2. 从 map 中删除，此时引用计数减 1
    size_t n = connections_.erase(conn->fd());
    LOG_INFO << "TcpServer::RemoveConnectionInLoop for fd=" << conn->fd() 
        << " thread id = " << std::this_thread::get_id() << " connections_.size() = " << connections_.size();
}
```



#### 完整代码
```cpp
#ifndef TCPSERVER_H
#define TCPSERVER_H

#include "net/Acceptor.h"
#include "net/TcpConnection.h"
#include "net/EventLoop.h"
#include "net/EventLoopThreadPool.h"
#include <map>
#include <memory>
#include <unordered_set>

class TcpServer : noncopyable
{
public:
	TcpServer(EventLoop* loop, const std::string& ip, uint16_t port, uint16_t conn_time_out);

	~TcpServer();

	void Start(int thread_num);
	void SetMessageCallback(TcpConnection::MessageCallback cb) { message_cb_ = cb; }
	void SetConnectionCallback(const TcpConnection::ConnectionCallback& cb) { connection_callback_ = cb; }

private:
	void NewConnection(int sockfd, const std::string& peerAddr);									// 处理新连接的回调
	void RemoveConnection(const std::shared_ptr<TcpConnection>& conn);
	void RemoveConnectionInLoop(const std::shared_ptr<TcpConnection>& conn);

	void Tick();																					// 时间轮的回调

private:
	void RefreshEntry(TcpConnection::EntryPtr entry);												// 刷新连接在时间轮里的超时索引

private:
	EventLoop* loop_ = nullptr;
	std::unique_ptr<Acceptor> acceptor_;

	// 连接池
	std::map<int, std::shared_ptr<TcpConnection>> connections_;

	TcpConnection::MessageCallback message_cb_;
    TcpConnection::ConnectionCallback connection_callback_;

	// 线程池
	std::unique_ptr<EventLoopThreadPool> thread_pool_;
	using TcpConnectionPtr = std::shared_ptr<TcpConnection>;

	// 时间轮
	using Bucket = std::unordered_set<TcpConnection::EntryPtr>;
	using TimingWheel = std::vector<Bucket>;

	TimingWheel wheel_;																				// 轮子的大小是连接的超时时间
	int wheel_size_;
	int wheel_curr_ = 0;
};

#endif
```



```cpp
#include "net/TcpServer.h"
#include "Log/Logger.h"
#include "net/ThreadSwitcher.h"

using namespace std::placeholders;

TcpServer::TcpServer(EventLoop* loop, const std::string& ip, uint16_t port, uint16_t conn_time_out)
    : loop_(loop),
    acceptor_(new Acceptor(loop, ip, port)),
    thread_pool_(std::make_unique<EventLoopThreadPool>(loop)),
    wheel_size_(conn_time_out)
{
    // 绑定新连接到来的处理函数
    acceptor_->SetNewConnectionCallback(std::bind(&TcpServer::NewConnection, this, _1, _2));
    
    // 初始化时间轮的大小
    wheel_.resize(wheel_size_);
}

TcpServer::~TcpServer()
{
    // 1. 设置标志位，防止析构过程中有新的连接进来
    //    或者在 Acceptor 中停止监听

    // 2. 遍历所有连接，通知它们进入 Shutdown 流程
    for (auto& item : connections_)
    {
        TcpConnectionPtr conn = item.second;

        // 关键点：将关闭任务投递到该连接所属的 IO 线程
        ThreadSwitcher::Run(conn->GetLoop(), conn, &TcpConnection::ShutDown);
    }

    // 3. 此时不能直接退出，需要等待连接真正完成关闭逻辑。
    //    Muduo 的做法是利用 Loop 的退出机制，但在简单的 TcpServer 中，
    //    你可以简单地让 EventLoop 等待一小会儿，或者通过引用计数判断。
}

void TcpServer::Start(int thread_num) 
{
    thread_pool_->SetThreadNum(thread_num);
    thread_pool_->Start();
    acceptor_->Listen();

    // 开始时间轮
    loop_->RunEvery(1.0, [this]() 
        {
        this->Tick(); // 时间轮，监测所有连接的超时
        });
}

void TcpServer::Tick()
{
    wheel_curr_ = (wheel_curr_ + 1) % wheel_.size();                                    // 挪动步长

    //for (const auto& entry : wheel_[wheel_curr_]) 
    //{
    //    LOG_INFO << "Tick at " << wheel_curr_
    //        <<  "entry use_count=" << entry.use_count();
    //}

    wheel_[wheel_curr_].clear();
}

void TcpServer::RefreshEntry(TcpConnection::EntryPtr entry)
{
    int target = (wheel_curr_ + wheel_.size() - 1) % wheel_.size();
    loop_->RunInLoop([this, target, entry]()
        {
            wheel_[target].insert(entry);
        });
}

void TcpServer::NewConnection(int sockfd, const std::string& peerAddr) 
{
    LOG_INFO << "TcpServer::NewConnection - new connection from " << peerAddr << "sockfd = " << sockfd;

    // 不再使用主线程的loop_
    EventLoop* io_loop = thread_pool_->GetNextLoop();
    EventLoop* loop = (io_loop != nullptr) ? io_loop : loop_;

    // 1. 创建 TcpConnection 对象
    auto conn = std::make_shared<TcpConnection>(loop, sockfd);

    // 2. 挪移到时间轮中管理超时
    TcpConnection::EntryPtr entry = std::make_shared<TcpConnection::Entry>(conn);
    conn->SetEntry(entry);
    RefreshEntry(entry);

    std::weak_ptr<TcpConnection::Entry> weak_entry = entry;

    // 3. 为连接设置消息回调
    auto wrapped_message_cb = [this, weak_entry](const TcpConnectionPtr& conn, Buffer* buf)
        {
            // 1. 续命：将 Entry 移到时间轮的最新桶
            auto entry = weak_entry.lock();
            if (entry)
            {
                RefreshEntry(entry);
            }

            // 2. 执行用户原始回调
            if (this->message_cb_) 
            {
                this->message_cb_(conn, buf);
            }
        };

    conn->SetMessageCallback(wrapped_message_cb);
    conn->SetConnectionCallback(connection_callback_);
    conn->SetCloseCallback(std::bind(&TcpServer::RemoveConnection, this, std::placeholders::_1));

    // 4. 将连接加入 map 管理
    connections_[sockfd] = std::move(conn);

    // 5. 向Epoll注册连接
    ThreadSwitcher::Run(loop, connections_[sockfd], &TcpConnection::ConnectEstablished);
}

void TcpServer::RemoveConnection(const std::shared_ptr<TcpConnection>& conn)
{
    ThreadSwitcher::Run(loop_, this, &TcpServer::RemoveConnectionInLoop, conn);
}

void TcpServer::RemoveConnectionInLoop(const std::shared_ptr<TcpConnection>& conn)
{
    loop_->AssertInLoopThread();

    // 1. 将最后的销毁动作放入队列
    // 注意：这里 bind 再次持有了 conn 的副本，引用计数+1，保证了 ConnectDestroyed 执行时对象不被析构
    ThreadSwitcher::Run(conn->GetLoop(), conn, &TcpConnection::ConnectDestroyed);

    // 2. 从 map 中删除，此时引用计数减 1
    size_t n = connections_.erase(conn->fd());
    LOG_INFO << "TcpServer::RemoveConnectionInLoop for fd=" << conn->fd() 
        << " thread id = " << std::this_thread::get_id() << " connections_.size() = " << connections_.size();
}
```



### TcpConnection
本质上是对Channel的又一层封装，不过这里只关注连接时间，比如各种事件的回调，连接的管理，连接的状态还有数据的读写于发送

#### 事件回调
```cpp
channel_->SetWriteCallback(std::bind(&TcpConnection::HandleWrite, this));
channel_->SetReadCallback(std::bind(&TcpConnection::HandleRead, this));
channel_->SetCloseCallback(std::bind(&TcpConnection::HandleClose, this));
channel_->SetErrorCallback(std::bind(&TcpConnection::HandleError, this));

void TcpConnection::HandleRead()
{
    loop_->AssertInLoopThread();
    int save_errno;
    ssize_t n = input_buffer_.ReadFd(socket_->fd(), &save_errno);
    if (n > 0)
    {
        if (message_callback_)
        {
            message_callback_(shared_from_this(), &input_buffer_);
        }
    }
    else if (n == 0)
    {
        HandleClose();
    }
    else
    {
        HandleError();
    }
}

void TcpConnection::HandleClose()
{
    loop_->AssertInLoopThread();
    SetState(kDisconnected);
    channel_->DisableAll();
    std::shared_ptr<TcpConnection> guard_this(shared_from_this());

    // 1.通知用户连接已经断开
    if (connection_callback_)
    {
        connection_callback_(guard_this);
    }

    // 2. 通知TcpServer移除连接
    if (close_callback_)
    {
        close_callback_(guard_this);
    }
}

void TcpConnection::HandleError()
{
    int err = socket_->GetTcpInfoError(); // 获取具体的内核错误码

    // wrk在测试结束时会发送RST包，为了快速回收资源
    if (err == ECONNRESET || err == ETIMEDOUT) 
    {
        LOG_INFO << "TcpConnection::HandleError [" << socket_->fd()
            << "] - Expected disconnect, SO_ERROR = " << err;
    }
    else
    {
        LOG_ERROR << "TcpConnection::HandleError [" << socket_->fd()
            << "] - Actual error, SO_ERROR = " << err << " " << strerror(err);
    }

    // 关键点：异常发生，立即触发关闭流程
    HandleClose();
}

void TcpConnection::HandleWrite()
{
    loop_->AssertInLoopThread();
    if (channel_->IsWriting())
    {
        // 1. 获取待发送数据的指针和长度
        const char* data = output_buffer_.peek();
        size_t len = output_buffer_.ReadableBytes();

        // 2. 尝试写入
        ssize_t nwrote = ::write(socket_->fd(), data, len);

        if (nwrote > 0)
        {
            // 3. 将已发送的部分从缓冲区移除
            output_buffer_.retrieve(nwrote);

            // 4. 如果缓冲区空了，说明发完了，必须停止关注写事件
            // 否则 epoll 会不停地触发 EPOLLOUT 事件，导致 CPU 占用 100%
            if (output_buffer_.ReadableBytes() == 0)
            {
                channel_->DisableWriting();

                // 如果用户之前调用了 Shutdown()，状态会变成 kDisconnecting
                // 此时数据发完了，我们可以安全地关闭写端了
                if (state_ == kDisconnecting)
                {
                    ShutDownInLoop();
                }

                LOG_INFO << "TcpConnection::HandleWrite finished sending all data.";
            }
        }
        else
        {
            LOG_ERROR << "TcpConnection::HandleWrite error";
        }
    }
}
```



#### 连接建立流程
server检测到连接到来，会映射到map中进行管理，然后调用ConnectEstablished

更新连接状态，并为channel设置一个weakptr，避免在处理fd事件时，连接销毁

最后调用业务中连接的回调，比如下面这样

<!-- 这是一张图片，ocr 内容为： -->
![](https://cdn.nlark.com/yuque/0/2026/png/54962239/1773139074775-e42f0fbc-455e-47fd-82a8-eeaa04f21483.png)

```cpp
void TcpConnection::ConnectEstablished()
{
    // 1. 设置状态为已连接
    SetState(kConnected);

    // 2. 绑定 Channel 与 TcpConnection 的生命周期
    // 这样在 Channel::HandleEvent 执行期间，TcpConnection 不会被析构
    channel_->tie(shared_from_this());

    // 3. 注册读事件到 Poller
    channel_->EnableReading();

    // 4. 通知用户连接已经建立
    if (connection_callback_)
    {
        connection_callback_(shared_from_this());
    }
}
```



#### 连接销毁
##### 主动销毁
如果连接超时，会触发强制关闭ForceClose

```cpp
TcpConnection::Entry::~Entry()
{
    if (auto conn = weak_conn_.lock())
    {
        // 避免僵尸连接导致无法收到对端返回的0，所以强制关闭连接
        LOG_INFO << "ForceClose Connection: " << conn->fd();
        conn->ForceClose();
    }
}
```



调用HandleClose，这里传的shared，其实不用传递也可以的，因为在server里面保留了一份映射传不传都无所谓，而且这个lambda又没有被一直持有，当这个函数处理完之后，loop的vector会清空的

```cpp
void TcpConnection::ForceClose()
{
    if (state_ == kConnected || state_ == kDisconnecting)
    {
        SetState(kDisconnecting);
        loop_->RunInLoop(std::bind(&TcpConnection::HandleClose, shared_from_this()));
    }
}
```



取消epoll中的所有监听，这里为什么需要用shared呢，因为在执行关闭回调时，会将server里面存储的映射移除，为了避免conn销毁掉，所以这里的shared是为了回调执行期间conn不会销毁，谁最后持有谁销毁

```cpp
void TcpConnection::HandleClose()
{
    loop_->AssertInLoopThread();
    SetState(kDisconnected);
    channel_->DisableAll();
    std::shared_ptr<TcpConnection> guard_this(shared_from_this());

    // 1.通知用户连接已经断开
    if (connection_callback_)
    {
        connection_callback_(guard_this);
    }

    // 2. 通知TcpServer移除连接
    if (close_callback_)
    {
        close_callback_(guard_this);
    }
}
```



很显然，conn的loop的任务队列中最后持有conn，因为这个函数执行完之后，conn就只剩下一份了，就是这个投递到loop的任务

```cpp
void TcpServer::RemoveConnection(const std::shared_ptr<TcpConnection>& conn)
{
    ThreadSwitcher::Run(loop_, this, &TcpServer::RemoveConnectionInLoop, conn);
}

void TcpServer::RemoveConnectionInLoop(const std::shared_ptr<TcpConnection>& conn)
{
    loop_->AssertInLoopThread();

    // 1. 将最后的销毁动作放入队列
    // 注意：这里 bind 再次持有了 conn 的副本，引用计数+1，保证了 ConnectDestroyed 执行时对象不被析构
    ThreadSwitcher::Run(conn->GetLoop(), conn, &TcpConnection::ConnectDestroyed);

    // 2. 从 map 中删除，此时引用计数减 1
    size_t n = connections_.erase(conn->fd());
    LOG_INFO << "TcpServer::RemoveConnectionInLoop for fd=" << conn->fd() 
        << " thread id = " << std::this_thread::get_id() << " connections_.size() = " << connections_.size();
}
```



最后执行的任务的lambda因为捕获了conn，保留最后一份引用，所以当loop执行任务队列的时候，整个conn都是生命安全的

```cpp
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
```



在这里又检查了一次是因为为了避免没有成功处理连接断开，做了一个收尾和验证，毕竟如果第一次没成功，也是会走到这里，然后不断的递归的，直到连接断开位置

最后是将该链接从红黑树上给移除掉并且关闭在poller中的映射

```cpp
void TcpConnection::ConnectDestroyed()
{
    loop_->AssertInLoopThread();

    if (state_ == kConnected)
    {
        SetState(kDisconnected);
        channel_->DisableAll();                                                         // 停止监听读写事件

        std::shared_ptr<TcpConnection> guard_this(shared_from_this());

        // 1.通知用户连接已经断开
        if (connection_callback_)
        {
            connection_callback_(guard_this);
        }

        // 2. 通知TcpServer移除连接
        if (close_callback_)
        {
            close_callback_(guard_this);
        }
    }

    channel_->Remove();                                                                 // 从 Poller 中彻底移除
}
```



移除映射

```cpp
void Channel::Remove()
{
    loop_->RemoveChannel(this);
}

void EventLoop::RemoveChannel(Channel* channel)
{
	AssertInLoopThread();
	poller_->RemoveChannel(channel);
}

void EPollPoller::RemoveChannel(Channel* channel)
{
	int fd = channel->fd();
	channels_.erase(fd);

	int index = channel->index();
	if (index == kAdded)
	{
		Update(EPOLL_CTL_DEL, channel);
	}

	channel->set_index(kNew);
}
```



##### 被动销毁
等待客户端发来断开信号，然后调用HandleClose，走上述的逻辑

```cpp
void TcpConnection::HandleRead()
{
    loop_->AssertInLoopThread();
    int save_errno;
    ssize_t n = input_buffer_.ReadFd(socket_->fd(), &save_errno);
    if (n > 0)
    {
        if (message_callback_)
        {
            message_callback_(shared_from_this(), &input_buffer_);
        }
    }
    else if (n == 0)
    {
        HandleClose();
    }
    else
    {
        HandleError();
    }
}
```



##### 优雅关闭
也是主动销毁的一种，为了让客户端将数据完整的读出，服务器关闭写端，如果服务器还在写，就等写完了之后再关闭写端

```cpp
void TcpConnection::ShutDown()
{
    if (state_ == kConnected)
    {
        SetState(kDisconnecting);
        loop_->RunInLoop(std::bind(&TcpConnection::ShutDownInLoop, this));
    }
}

void TcpConnection::ShutDownInLoop()
{
    loop_->AssertInLoopThread();
    
    // 没有监听写事件
    if (!channel_->IsWriting())
    {
        socket_->ShutdownWrite();                                                       // 关闭写端                               
    }

    // 如果在写数据，等数据写完后，在ShutDown
}

void TcpConnection::HandleWrite()
{
    loop_->AssertInLoopThread();
    if (channel_->IsWriting())
    {
        // 1. 获取待发送数据的指针和长度
        const char* data = output_buffer_.peek();
        size_t len = output_buffer_.ReadableBytes();

        // 2. 尝试写入
        ssize_t nwrote = ::write(socket_->fd(), data, len);

        if (nwrote > 0)
        {
            // 3. 将已发送的部分从缓冲区移除
            output_buffer_.retrieve(nwrote);

            // 4. 如果缓冲区空了，说明发完了，必须停止关注写事件
            // 否则 epoll 会不停地触发 EPOLLOUT 事件，导致 CPU 占用 100%
            if (output_buffer_.ReadableBytes() == 0)
            {
                channel_->DisableWriting();

                // 如果用户之前调用了 Shutdown()，状态会变成 kDisconnecting
                // 此时数据发完了，我们可以安全地关闭写端了
                if (state_ == kDisconnecting)
                {
                    ShutDownInLoop();
                }

                LOG_INFO << "TcpConnection::HandleWrite finished sending all data.";
            }
        }
        else
        {
            LOG_ERROR << "TcpConnection::HandleWrite error";
        }
    }
}
```



#### 完整代码
```cpp
#ifndef TCPCONNECTION_H
#define TCPCONNECTION_H

#include "noncopyable.h"
#include "net/Channel.h"
#include "net/Socket.h"
#include "net/Buffer.h"
#include <memory>
#include <string>
#include <functional>

class EventLoop;
class TcpConnection : public std::enable_shared_from_this<TcpConnection>, noncopyable
{
public:
	struct Entry {
		std::weak_ptr<TcpConnection> weak_conn_;
		Entry(const std::weak_ptr<TcpConnection>& weakConn) : weak_conn_(weakConn) {}

		// 关键点：析构函数必须在 .cpp 中实现，因为此时需要知道 TcpConnection 的完整定义
		~Entry();
	};
	using EntryPtr = std::shared_ptr<Entry>;

public:
	enum StateE { kDisconnected, kConnecting, kConnected, kDisconnecting };
	// 在 TcpConnection.h 中
	using MessageCallback = std::function<void(const std::shared_ptr<TcpConnection>&, Buffer*)>;
	using ConnectionCallback = std::function<void(const std::shared_ptr<TcpConnection>&)>;
	using CloseCallback = std::function<void(const std::shared_ptr<TcpConnection>&)>;

	TcpConnection(EventLoop* loop, int socket_fd);
	~TcpConnection();

	// 连接管理
	void ConnectEstablished();													// 连接建立时调用
	void ConnectDestroyed();													// 连接销毁时调用
	void SetState(StateE s) { state_ = s; }										// 连接状态

	// 连接状态
	bool Connected() const { return state_ == kConnected; }
	bool DisConnected() const { return state_ == kDisconnected; }

	int fd() const { return socket_->fd(); }

	// 消息回调
	void SetMessageCallback(MessageCallback cb) { message_callback_ = std::move(cb); }
	void SetConnectionCallback(const ConnectionCallback& cb) { connection_callback_ = cb; }
	void SetCloseCallback(const CloseCallback& cb) { close_callback_ = cb; }

	// 发送数据
	void Send(const std::string& data);
	void SendInLoop(const std::string& data);

	// 获取loop
	EventLoop* GetLoop() { return loop_; }

	// 手动断开连接
	/*
	* 关闭写端，读端检测到写端关闭，待数据读完之后，会返回给服务器0
	* 服务器read 0，会调用HandleClose关闭连接
	*/
	void ShutDown();

	/*
	* 强制调用HandleClose，直接从loop中移除channel
	*/
	void ForceClose();															// 强制关闭

	// 时间轮
	void SetEntry(const EntryPtr& entry) { entry_ = entry; }

private:
	void HandleRead();															// 处理读事件
	void HandleWrite();															// 处理写事件	
	void HandleClose();															// 处理关闭事件
    void HandleError();															// 处理错误事件
	void ShutDownInLoop();														// 在loop中关闭连接

private:
	EventLoop* loop_;															// 所属的EventLoop										
	std::unique_ptr<Socket> socket_;
	std::unique_ptr<Channel> channel_;
	
	MessageCallback message_callback_;
    ConnectionCallback connection_callback_;
	CloseCallback close_callback_;

	Buffer output_buffer_;														// 写数据缓冲
	Buffer input_buffer_;														// 读数据缓冲

	StateE state_;																// 连接状态

	std::weak_ptr<Entry> entry_;												// conn的包装器，用于挪移到时间轮的桶子里
};

#endif
```

```cpp
#include "net/TcpConnection.h"
#include "Log/Logger.h"
#include "net/EventLoop.h"
#include <unistd.h>
#include <errno.h>

TcpConnection::TcpConnection(EventLoop* loop, int sockfd)
    : loop_(loop),
    socket_(new Socket(sockfd)),
    channel_(new Channel(loop, sockfd)),
    state_(kConnecting)
{
    channel_->SetWriteCallback(std::bind(&TcpConnection::HandleWrite, this));
    channel_->SetReadCallback(std::bind(&TcpConnection::HandleRead, this));
    channel_->SetCloseCallback(std::bind(&TcpConnection::HandleClose, this));
    channel_->SetErrorCallback(std::bind(&TcpConnection::HandleError, this));
}

TcpConnection::~TcpConnection()
{
    LOG_INFO << "TcpConnection::~TcpConnection fd=" << socket_->fd();
}

void TcpConnection::ConnectEstablished()
{
    // 1. 设置状态为已连接
    SetState(kConnected);

    // 2. 绑定 Channel 与 TcpConnection 的生命周期
    // 这样在 Channel::HandleEvent 执行期间，TcpConnection 不会被析构
    channel_->tie(shared_from_this());

    // 3. 注册读事件到 Poller
    channel_->EnableReading();

    // 4. 通知用户连接已经建立
    if (connection_callback_)
    {
        connection_callback_(shared_from_this());
    }
}

void TcpConnection::Send(const std::string& data)
{
    if (Connected())
    {
        // 同线程直接发送
        if (loop_->IsInLoopThread())
        {
            SendInLoop(data);
        }
        else
        {
            // conn与调用者不在同一个线程，将该任务投递到conn所在的线程中执行
            loop_->RunInLoop(std::bind(&TcpConnection::SendInLoop, this, data));
        }
    }
}

void TcpConnection::SendInLoop(const std::string& data)
{
    loop_->AssertInLoopThread();
    ssize_t nwrote = 0;

    // 如果之前没有数据再发，且当前的缓冲区为空，尝试直接发送
    if (!channel_->IsWriting() && output_buffer_.ReadableBytes() == 0)
    {
        nwrote = ::write(socket_->fd(), data.data(), data.size());
        if (nwrote >= 0)
        {
            if (nwrote < data.size())
            {
                LOG_WARN << "Write only " << nwrote << " bytes instead of " << data.size();
            }
        }
        else
        {
            nwrote = 0;
            if (errno != EWOULDBLOCK)
            {

            }
        }
    }

    // 如果没有写完，将剩余的数据写入缓冲区，并关注写事件
    if (static_cast<size_t>(nwrote) < data.size())
    {
        output_buffer_.Append(data.data() + nwrote, data.size() - nwrote);
        if (!channel_->IsWriting())
        {
            channel_->EnableWriting();
        }
    }
}

void TcpConnection::ShutDown()
{
    if (state_ == kConnected)
    {
        SetState(kDisconnecting);
        loop_->RunInLoop(std::bind(&TcpConnection::ShutDownInLoop, this));
    }
}

void TcpConnection::ForceClose()
{
    if (state_ == kConnected || state_ == kDisconnecting)
    {
        SetState(kDisconnecting);
        loop_->RunInLoop(std::bind(&TcpConnection::HandleClose, shared_from_this()));
    }
}

void TcpConnection::HandleRead()
{
    loop_->AssertInLoopThread();
    int save_errno;
    ssize_t n = input_buffer_.ReadFd(socket_->fd(), &save_errno);
    if (n > 0)
    {
        if (message_callback_)
        {
            message_callback_(shared_from_this(), &input_buffer_);
        }
    }
    else if (n == 0)
    {
        HandleClose();
    }
    else
    {
        HandleError();
    }
}

void TcpConnection::HandleClose()
{
    loop_->AssertInLoopThread();
    SetState(kDisconnected);
    channel_->DisableAll();
    std::shared_ptr<TcpConnection> guard_this(shared_from_this());

    // 1.通知用户连接已经断开
    if (connection_callback_)
    {
        connection_callback_(guard_this);
    }

    // 2. 通知TcpServer移除连接
    if (close_callback_)
    {
        close_callback_(guard_this);
    }
}

void TcpConnection::HandleError()
{
    int err = socket_->GetTcpInfoError(); // 获取具体的内核错误码

    // wrk在测试结束时会发送RST包，为了快速回收资源
    if (err == ECONNRESET || err == ETIMEDOUT) 
    {
        LOG_INFO << "TcpConnection::HandleError [" << socket_->fd()
            << "] - Expected disconnect, SO_ERROR = " << err;
    }
    else
    {
        LOG_ERROR << "TcpConnection::HandleError [" << socket_->fd()
            << "] - Actual error, SO_ERROR = " << err << " " << strerror(err);
    }

    // 关键点：异常发生，立即触发关闭流程
    HandleClose();
}

void TcpConnection::HandleWrite()
{
    loop_->AssertInLoopThread();
    if (channel_->IsWriting())
    {
        // 1. 获取待发送数据的指针和长度
        const char* data = output_buffer_.peek();
        size_t len = output_buffer_.ReadableBytes();

        // 2. 尝试写入
        ssize_t nwrote = ::write(socket_->fd(), data, len);

        if (nwrote > 0)
        {
            // 3. 将已发送的部分从缓冲区移除
            output_buffer_.retrieve(nwrote);

            // 4. 如果缓冲区空了，说明发完了，必须停止关注写事件
            // 否则 epoll 会不停地触发 EPOLLOUT 事件，导致 CPU 占用 100%
            if (output_buffer_.ReadableBytes() == 0)
            {
                channel_->DisableWriting();

                // 如果用户之前调用了 Shutdown()，状态会变成 kDisconnecting
                // 此时数据发完了，我们可以安全地关闭写端了
                if (state_ == kDisconnecting)
                {
                    ShutDownInLoop();
                }

                LOG_INFO << "TcpConnection::HandleWrite finished sending all data.";
            }
        }
        else
        {
            LOG_ERROR << "TcpConnection::HandleWrite error";
        }
    }
}

void TcpConnection::ShutDownInLoop()
{
    loop_->AssertInLoopThread();
    
    // 没有监听写事件
    if (!channel_->IsWriting())
    {
        socket_->ShutdownWrite();                                                       // 关闭写端                               
    }

    // 如果在写数据，等数据写完后，在ShutDown
}

void TcpConnection::ConnectDestroyed()
{
    loop_->AssertInLoopThread();

    if (state_ == kConnected)
    {
        SetState(kDisconnected);
        channel_->DisableAll();                                                         // 停止监听读写事件

        std::shared_ptr<TcpConnection> guard_this(shared_from_this());

        // 1.通知用户连接已经断开
        if (connection_callback_)
        {
            connection_callback_(guard_this);
        }

        // 2. 通知TcpServer移除连接
        if (close_callback_)
        {
            close_callback_(guard_this);
        }
    }

    channel_->Remove();                                                                 // 从 Poller 中彻底移除
}

TcpConnection::Entry::~Entry()
{
    if (auto conn = weak_conn_.lock())
    {
        // 避免僵尸连接导致无法收到对端返回的0，所以强制关闭连接
        LOG_INFO << "ForceClose Connection: " << conn->fd();
        conn->ForceClose();
    }
}
```



### 多线程
多线程就是每个线程有一个独立的loop，主线程的loop职责是监听新的连接，也就是listenfd

子线程只需要负责监听连接的消息，然后执行连接的任务即可

子线程的ThreadFunc，创建一个loop对象，然后开始loop

线程池的作用就是创建子线程，然后调用每个线程的StartLoop，获取每个子线程的loop，并保存起来，方便下次分配给新的fd



#### EventLoopThread
##### StartLoop
在线程池里面操作每个线程，肯定是要加锁的，这里锁的目的，是为了获取线程里的loop，

由于ThreadFunc是在子线程执行的，而StartLoop是在主线程中执行的，所以主线程要阻塞等待loop初始化完毕

```cpp
EventLoop* EventLoopThread::StartLoop()
{
	thread_ = std::thread(&EventLoopThread::ThreadFunc, this);
	EventLoop* loop = nullptr;
	{
        std::unique_lock<std::mutex> lock(mutex_);
		// 等待loop_初始化完成
        cond_.wait(lock, [this]() { return loop_ != nullptr; });
		loop = loop_;
	}
	return loop;
}

void EventLoopThread::ThreadFunc()
{
	// 创建一个loop对象
	EventLoop loop;

	{
		std::unique_lock<std::mutex> lock(mutex_);
		loop_ = &loop;
		cond_.notify_one();													// 通知 startLoop 的主线程
	}

    loop.Loop();															// 开启事件循环

	// 退出循环清理资源
	std::unique_lock<std::mutex> lock(mutex_);
	loop_ = nullptr;
}
```



##### 完整代码
```cpp
#ifndef EVENT_LOOP_THREAD_H
#define EVENT_LOOP_THREAD_H
#include "noncopyable.h"
#include <thread>
#include <mutex>
#include <condition_variable>

class EventLoop;
class EventLoopThread : noncopyable
{
public:
	EventLoopThread();
	~EventLoopThread();
    EventLoop* StartLoop();

private:
	void ThreadFunc();

private:
	EventLoop* loop_;
	std::thread thread_;
	std::mutex mutex_;
	std::condition_variable cond_;
};

#endif
```

```cpp
#include "net/EventLoopThread.h"
#include "net/EventLoop.h"

EventLoopThread::EventLoopThread()
	: loop_(nullptr),
	mutex_(),
	cond_()
{

}

EventLoopThread::~EventLoopThread()
{
	if (loop_ != nullptr)
	{
		loop_->Quit();												// 停止Loop
		if (thread_.joinable())
		{
			thread_.join();
		}
	}
}

EventLoop* EventLoopThread::StartLoop()
{
	thread_ = std::thread(&EventLoopThread::ThreadFunc, this);
	EventLoop* loop = nullptr;
	{
        std::unique_lock<std::mutex> lock(mutex_);
		// 等待loop_初始化完成
        cond_.wait(lock, [this]() { return loop_ != nullptr; });
		loop = loop_;
	}
	return loop;
}

void EventLoopThread::ThreadFunc()
{
	// 创建一个loop对象
	EventLoop loop;

	{
		std::unique_lock<std::mutex> lock(mutex_);
		loop_ = &loop;
		cond_.notify_one();													// 通知 startLoop 的主线程
	}

    loop.Loop();															// 开启事件循环

	// 退出循环清理资源
	std::unique_lock<std::mutex> lock(mutex_);
	loop_ = nullptr;
}
```





#### EventLoopThreadPool
线程池启动的时候根据设置的线程的数量创建线程，然后存储每一个线程的loop，用于fd的分配



##### Start
```cpp
void EventLoopThreadPool::Start() 
{
    for (int i = 0; i < num_threads_; ++i)
    {
        auto t = std::make_unique<EventLoopThread>();
        loops_.push_back(t->StartLoop());
        threads_.push_back(std::move(t));
    }
}
```



##### 完整代码
```cpp
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
```

```cpp
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
```



#### 应用
在server中存储一个线程池，新连接到来时，会获取该线程池的poll，来作为连接的事件循环

```cpp
std::unique_ptr<EventLoopThreadPool> thread_pool_;
void TcpServer::Start(int thread_num) 
{
    thread_pool_->SetThreadNum(thread_num);
    thread_pool_->Start();
    acceptor_->Listen();

    // 开始时间轮
    loop_->RunEvery(1.0, [this]() 
        {
        this->Tick(); // 时间轮，监测所有连接的超时
        });
}

void TcpServer::NewConnection(int sockfd, const std::string& peerAddr) 
{
    LOG_INFO << "TcpServer::NewConnection - new connection from " << peerAddr << "sockfd = " << sockfd;

    // 不再使用主线程的loop_
    EventLoop* io_loop = thread_pool_->GetNextLoop();
    EventLoop* loop = (io_loop != nullptr) ? io_loop : loop_;
}
```



### 时间轮
#### 问题
比如，如果有5000个连接，每个连接都需要管理心跳，当超时时能够及时的清除，既不能为5000个连接都创建定时器，也不能在server中创建一个定时任务，去轮询这5000个连接，首先是精度不够，其次这个任务太过于庞大了，所以采用时间轮的方式



#### 解决思路
思考：

<!-- 这是一张图片，ocr 内容为： -->
![](https://cdn.nlark.com/yuque/0/2026/png/54962239/1773042796509-184b6965-91d1-4011-bfa6-6f5cc9de183f.png)

依旧是轮询的方式，每一秒回调当前cur下的桶里的所有连接

将连接分配到哪个桶里，这个分配行为只有1个触发情况

当连接刚接收到消息时，就分配索引为8的那个桶里面

当过去1s之后，这个连接的索引就应该改变了，但是为连接修改索引，或者将连接挪到索引为7的桶里就过于麻烦

不要忘了，我们是每一秒往前迈一步，所以距离索引为8的位置就只剩7个了，所以超时的时间被映射成了步长，我们什么都不需要修改，只需要在超时时间为0并处理了所有连接时，将步长拉成8即可，怎么拉长步长简单回到开头，也就是循环队列



结论：

从上面的思考，我总结出一个核心概念，超时时间 == 当前位置到超时位置的步长

所以当连接接收到新的消息就需要将该连接挪动到距离当前索引步长为8的位置

这就是时间轮!!!



实践：

还是要在server里面维护一个定时任务，去定时的轮询每个位置，到这个位置就需要把位置上的所有连接清空掉，但是又不能是遍历，如果每个位置有1000个，岂不是要遍历1000个，太麻烦了，所以直接封装一个包装器，利用容器set的clear，然后容器中存储shared的包装器，就能让引用计数归0，自己调用包装器的析构函数，然后调用连接的强制关闭



#### 包装器Entry
```cpp
struct Entry {
	std::weak_ptr<TcpConnection> weak_conn_;
	Entry(const std::weak_ptr<TcpConnection>& weakConn) : weak_conn_(weakConn) {}
	
    // 关键点：析构函数必须在 .cpp 中实现，因为此时需要知道 TcpConnection 的完整定义
	~Entry();
};
using EntryPtr = std::shared_ptr<Entry>;

TcpConnection::Entry::~Entry()
{
    if (auto conn = weak_conn_.lock())
    {
        // 避免僵尸连接导致无法收到对端返回的0，所以强制关闭连接
        LOG_INFO << "ForceClose Connection: " << conn->fd();
        conn->ForceClose();
    }
}
```



#### 容器
unordered_set能够自动排序

```cpp
// 时间轮
using Bucket = std::unordered_set<TcpConnection::EntryPtr>;
using TimingWheel = std::vector<Bucket>;

TimingWheel wheel_;																				// 轮子的大小是连接的超时时间
int wheel_size_;
int wheel_curr_ = 0;

TcpServer::TcpServer(EventLoop* loop, const std::string& ip, uint16_t port, uint16_t conn_time_out)
    : loop_(loop),
    acceptor_(new Acceptor(loop, ip, port)),
    thread_pool_(std::make_unique<EventLoopThreadPool>(loop)),
    wheel_size_(conn_time_out)
{
    // 绑定新连接到来的处理函数
    acceptor_->SetNewConnectionCallback(std::bind(&TcpServer::NewConnection, this, _1, _2));
    
    // 初始化时间轮的大小
    wheel_.resize(wheel_size_);
}
```



#### 连接超时位置的更新
连接只有消息到来时，才会更新位置，当然我们用的sharedptr，所以只需要把连接的包装器直接添加到新的超时位置(距离当前索引超时时间步长的位置)

```cpp
void TcpServer::RefreshEntry(TcpConnection::EntryPtr entry)
{
    int target = (wheel_curr_ + wheel_.size() - 1) % wheel_.size();
    loop_->RunInLoop([this, target, entry]()
        {
            wheel_[target].insert(entry);
        });
}
```



#### 定时器轮询
到这个位置就果断的将set清空

```cpp
void TcpServer::Tick()
{
    wheel_curr_ = (wheel_curr_ + 1) % wheel_.size();                                    // 挪动步长

    //for (const auto& entry : wheel_[wheel_curr_]) 
    //{
    //    LOG_INFO << "Tick at " << wheel_curr_
    //        <<  "entry use_count=" << entry.use_count();
    //}

    wheel_[wheel_curr_].clear();
}
```



#### Bug
连接并没有被清空，通过打印引用计数发现，再超时时间清空的时候，引用计数是2

why？

是因为在设置连接消息回调时，我们的lambda捕获的是shared，你记住，lambda也是可以当做成员变量被存储起来的，而这个lambda是MessageCallback，也就是conn的成员变量

只要conn不销毁，该变量就一直有效，他持有的Entry的shared也会一直有效

所以我们不能捕获shared，应该捕获weakptr，将shared传成weakptr，就解决了

```cpp
std::weak_ptr<TcpConnection::Entry> weak_entry = entry;
```



完整代码

```cpp
TcpConnection::EntryPtr entry = std::make_shared<TcpConnection::Entry>(conn);
conn->SetEntry(entry);
RefreshEntry(entry);

std::weak_ptr<TcpConnection::Entry> weak_entry = entry;

// 3. 为连接设置消息回调
auto wrapped_message_cb = [this, weak_entry](const TcpConnectionPtr& conn, Buffer* buf)
    {
        // 1. 续命：将 Entry 移到时间轮的最新桶
        auto entry = weak_entry.lock();
        if (entry)
        {
            RefreshEntry(entry);
        }

        // 2. 执行用户原始回调
        if (this->message_cb_) 
        {
            this->message_cb_(conn, buf);
        }
    };
```





### TcpClient
#### 背景
为什么写TcpClient，是因为在使用rpc通信时，当网关服务进行消息转发时，会调用channel，发送消息，此时需要阻塞等待响应，如果设置超时的话这个响应包也是要读取的，所以就为每一个channel引入了后台线程，专门进行收包，确保每一个包都是干净的，即便超时了，丢弃就行

线程间的通信引入了std::promise以及std::future，和wakeupfd一个道理；

但是不可避免的是网关服务负责调用服务方法的线程会陷入阻塞，因为是同步的，当然也可以变成异步的

也就是不再阻塞等待响应，但是我们在请求服务的时候，由于每个服务只维护一个socket，所以需要加锁写入

如果都是轻量化的请求，比如登录，聊天呀这种，锁的开销带来的延迟可以忽略，但是如果变成文件传输，占用锁就会导致别的线程在请求该服务时不可避免的陷入阻塞，从而导致其余的请求的延迟

比如两个线程都要请求文件传输服务，线程A获取锁了，线程B只能阻塞等待，但是如果为这个socket维护一个消息队列就不会了，只需要对消息队列加锁就行，但是最后还是会涉及到锁的开销

所以为了避免锁的开销带来的阻塞，引入了TcpClient，也就是为每一个服务之前通信的socket引入了事件循环，再进行发包的时候，只需要触发socket，也就是conn的send事件，因为每一个发送缓冲区，只有256k，如果是一个1m的包，需要写入发送缓冲区，阻塞等待发送缓冲区写完，在写入发送缓冲区，在阻塞，重复3次

但是这里只会发送一次，然后把没发完的数据存储到buffer里，等发送缓冲区清空，触发写事件，继续写剩下的重复以往

区别就是之前发送1mb的包要一直阻塞在这里，无法处理别的请求，也就是发送缓冲区发送数据这段时间是忙等的

但是现在不再是忙等的，发送缓冲区刚写满，就结束锁了，去处理下一个触发的事件

因此延迟被大大的平均了，而且读事件也是有内核进行监听的，避免了线程的开销



#### 思路
一个tcpclient需要什么？需要一个socket

tcpserver的socket是从accept里分配出来的，而client的呢，只能够自己创建；

我们对这个socket的定义是什么，是当连接没有建立成功或者连接超时后，连接会自动重新尝试重连，直到连上为止



所以就需要在连接失败时有一个重新尝试连接的函数，也就是Retry



#### Connector
```cpp
#ifndef MYMUDUO_CONNECTOR_H
#define MYMUDUO_CONNECTOR_H

#include "net/EventLoop.h"
#include "net/Channel.h"
#include <functional>
#include <memory>
#include <atomic>
class Connector;
using ConnectorPtr = std::shared_ptr<Connector>;

class Connector : public std::enable_shared_from_this<Connector>
{
public:
using NewConnectionCallback = std::function<void(int sockfd)>;

Connector(EventLoop* loop, const std::string& ip, uint16_t port);
~Connector();

void SetNewConnectionCallback(const NewConnectionCallback& cb) { new_connection_callback_ = cb; }
void Start();																							// 启动连接
void Stop();																							// 停止连接

std::string GetIp() const { return ip_; }
int GetPort() const { return port_; }

private:
void StartInLoop();
void StopInLoop();
void Connect();
void Connecting(int sockfd);
void HandleWrite(); // 核心：epoll 侦测到连接建立时的可写事件
void HandleError();
void Retry(int sockfd);
int RemoveAndResetChannel();
void ResetChannel();

private:
enum States { kDisconnected, kConnecting, kConnected };
void SetState(States s) { state_ = s; }

private:
EventLoop* loop_;
std::string  ip_;
uint16_t port_;

std::atomic<bool> connect_;
std::atomic<States> state_;
std::unique_ptr<Channel> channel_; // 专门用来监听 connect 状态的通道
NewConnectionCallback new_connection_callback_;
};

#endif
```



```cpp
#include "net/Connector.h"
#include "Log/Logger.h"
#include <sys/socket.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <netinet/in.h>
#include <arpa/inet.h>


// 创建非阻塞 Socket
static int CreateNonblocking()
{
    int sockfd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, IPPROTO_TCP);
    if (sockfd < 0) 
    {
        LOG_FATAL << "socket create failed!";
    }
    return sockfd;
}

Connector::Connector(EventLoop* loop, const std::string& ip, uint16_t port)
: loop_(loop), ip_(ip), port_(port), connect_(false), state_(kDisconnected)
{

}

Connector::~Connector()
{
}

void Connector::Start() 
{
    connect_ = true;
    loop_->RunInLoop(std::bind(&Connector::StartInLoop, this));
}

void Connector::Stop()
{
    // 1. 立刻把原子标志位设为 false，彻底切断重连的念想
    connect_ = false;

    // 2. 把真正的清理工作，扔给 EventLoop 所在的线程去安全执行
    // 必须用 QueueInLoop，防止在多线程环境下直接操作底层 Channel 导致崩溃
    loop_->QueueInLoop(std::bind(&Connector::StopInLoop, this));
}

void Connector::StartInLoop()
{
    loop_->AssertInLoopThread();
    if (connect_) 
    {
        Connect();
    }
}

void Connector::StopInLoop()
{
    loop_->AssertInLoopThread();

    // 只有在 "正在尝试连接中 (kConnecting)" 的状态下，才需要我们去擦屁股。
    // 如果是 kConnected，说明 fd 已经移交给 TcpConnection 了，归别人管了。
    // 如果是 kDisconnected，说明本来就没连，什么都不用做。
    if (state_ == kConnecting)
    {
        SetState(kDisconnected);

        // 1. 把插在 epoll 上的监听通道拔下来
        int sockfd = RemoveAndResetChannel();

        // 2. 神级复用：借用 Retry 函数来关掉底层的 Socket！
        // 仔细看你的 Retry 函数，它第一行就是 ::close(sockfd);
        // 然后它会判断 if (connect_) 才去重连。
        // 因为我们在 Stop() 里已经把 connect_ 设置为 false 了，
        // 所以 Retry 在这里只会乖乖地关掉 fd，绝对不会触发下一次重试！
        Retry(sockfd);
    }
}

void Connector::Connect() 
{
    int sockfd = CreateNonblocking();

    // 发起非阻塞的 connect
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port_);
    server_addr.sin_addr.s_addr = inet_addr(ip_.c_str());

    int ret = ::connect(sockfd, (struct sockaddr*)&server_addr, sizeof(sockaddr_in));
    int savedErrno = (ret == 0) ? 0 : errno;

    switch (savedErrno) 
        {
            case 0:
            case EINPROGRESS: // 重点：异步连接正在进行中！交给 epoll 去盯梢！
            case EINTR:
            case EISCONN:
                Connecting(sockfd);
                break;
            case EAGAIN:
            case EADDRINUSE:
            case EADDRNOTAVAIL:
            case ECONNREFUSED:
            case ENETUNREACH:
                Retry(sockfd);
                break;
            default:
                LOG_ERROR << "Unexpected error in connect() " << savedErrno;
                ::close(sockfd);
                break;
        }
}

void Connector::Connecting(int sockfd) 
{
    SetState(kConnecting);
    // 创建一个 Channel，挂在当前的 EventLoop 上
    channel_.reset(new Channel(loop_, sockfd));

    // 当 socket 变得可写时，说明连接建立成功（或者报错了）
    channel_->SetWriteCallback(std::bind(&Connector::HandleWrite, this));
    channel_->SetErrorCallback(std::bind(&Connector::HandleError, this));

    // 告诉 epoll：帮我盯着这个 fd 的可写事件 (EPOLLOUT)
    channel_->EnableWriting();
}

// 三次握手结束后，就会触发写事件
void Connector::HandleWrite() 
{
    LOG_INFO << "Connector::handleWrite " << state_;
    if (state_ == kConnecting) 
    {
        int sockfd = RemoveAndResetChannel();                                               // 拔出插头，这个 fd 以后要交给 TcpConnection 了

        // 检查是不是真的连上了（获取底层错误码）
        int err = 0;
        socklen_t optlen = sizeof(err);
        if (::getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &err, &optlen) < 0) {
            err = errno;
        }

        if (err) 
        {
            LOG_ERROR << "Connector::handleWrite - SO_ERROR = " << err;
            Retry(sockfd);
        }
        else
        {
            SetState(kConnected);
            if (connect_)
            {
                // 连接成功！通知大当家 TcpClient 去创建 TcpConnection！
                if(new_connection_callback_)
                    new_connection_callback_(sockfd);
            }
            else 
            {
                ::close(sockfd);
            }
        }
    }
}

void Connector::HandleError() 
{
    LOG_ERROR << "Connector::handleError";
    if (state_ == kConnecting)
    {
        int sockfd = RemoveAndResetChannel();
        Retry(sockfd);
    }
}

void Connector::Retry(int sockfd)
{
    ::close(sockfd);
    SetState(kDisconnected);
    if (connect_)
    {
        LOG_INFO << "Connector::retry - connecting to " << ip_ << " " << port_ << " in 2 seconds...";
        // 此处为了简化工程，休眠一会儿然后投递重试任务，实际可配合 TimerQueue
        loop_->RunAfter(2.0, std::bind(&Connector::StartInLoop, shared_from_this()));
    }
}

int Connector::RemoveAndResetChannel() 
{
    channel_->DisableAll();
    channel_->Remove();
    int sockfd = channel_->fd();
    
    // 把任务推迟到下一个事件循环销毁 channel，防止死锁
    loop_->QueueInLoop(std::bind(&Connector::ResetChannel, this));
    return sockfd;
}

void Connector::ResetChannel()
{
    channel_.reset();
}
```



#### 启动连接
```cpp
void TcpClient::Connect()
{
    connect_ = true;
    connector_->Start();
}
```



#### 设置连接回调
也就是当socket成功建立连接之后，将消息回调，关闭回调等等，都转交给TcpConnection

```cpp
void TcpClient::NewConnection(int sockfd)
{
    loop_->AssertInLoopThread();

    TcpConnectionPtr conn = std::make_shared<TcpConnection>(loop_, sockfd);

    // 设置回调
    conn->SetConnectionCallback(connection_callback_);
    conn->SetMessageCallback(message_callback_);
    conn->SetWriteCompleteCallback(write_complete_callback_);

    // 断开清理的回调
    conn->SetCloseCallback(std::bind(&TcpClient::RemoveConnection, this, _1));

    {
        std::lock_guard<std::mutex> lock(mutex_);
        connection_ = conn;
    }

    connection_->ConnectEstablished();
}
```



#### 超时重连
服务器在长时间接收不到对端的信息，会进行超时断开，当然如果业务不在需要重连，可以提前设置retry为false

```cpp
void TcpClient::RemoveConnection(const TcpConnectionPtr& conn)
{
    loop_->AssertInLoopThread();

    ThreadSwitcher::Run(conn->GetLoop(), conn, &TcpConnection::ConnectDestroyed);

    {
        std::lock_guard<std::mutex> lock(mutex_);
        connection_.reset();
    }

    // 检查是否是超时断开，如果不是业务端的连接断开，自动重新连接
    if (retry_ && connect_)
    {
        LOG_INFO << "TcpClient::RemoveConnection - Reconnecting to " << connector_->GetIp() << " " << connector_->GetPort();
        // 让工兵 Connector 重新挂到 epoll 上去发起非阻塞 connect！
        connector_->Start();
    }
}
```



#### 完整代码
```cpp
#ifndef MYMUDUO_TCPCLIENT_H
#define MYMUDUO_TCPCLIENT_H

#include "net/EventLoop.h"
#include "net/TcpConnection.h"
#include "net/Connector.h"
#include <mutex>

class TcpClient : noncopyable
{
public:
	TcpClient(EventLoop* loop, const std::string& ip, uint16_t port, const std::string& name_arg);

	~TcpClient();

	void Connect();
	void Disconnect();
	void Stop();

	TcpConnectionPtr Connection() const
	{
		std::lock_guard<std::mutex> lock(mutex_);
		return connection_;
	}

    EventLoop* getLoop() const { return loop_; }
    const std::string& name() const { return name_; }
    void SetRetry(bool bRetry) { retry_ = bRetry; }

    void SetConnectionCallback(ConnectionCallback cb) { connection_callback_ = std::move(cb); }
    void SetMessageCallback(MessageCallback cb) { message_callback_ = std::move(cb); }
    void SetWriteCompleteCallback(WriteCompleteCallback cb) { write_complete_callback_ = std::move(cb); }

private:
    void NewConnection(int sockfd);
    void RemoveConnection(const TcpConnectionPtr& conn);

private:
    EventLoop* loop_;
    ConnectorPtr connector_;
    const std::string name_;

    ConnectionCallback connection_callback_;
    MessageCallback message_callback_;
    WriteCompleteCallback write_complete_callback_;

    std::atomic<bool> retry_;                                                                       // 是否重新连接
    std::atomic<bool> connect_;                                                                     // 业务层是否主动断开连接

    int nextConnId_;
    mutable std::mutex mutex_;
    TcpConnectionPtr connection_;
};

#endif
```



```cpp
#include "net/TcpClient.h"
#include "Log/Logger.h"
#include <sys/socket.h>
#include "net/ThreadSwitcher.h"

using namespace std::placeholders;

TcpClient::TcpClient(EventLoop* loop, const std::string& ip, uint16_t port, const std::string& name_arg)
    : loop_(loop),
      connector_(new Connector(loop, ip, port)),
      name_(name_arg),
      connect_(true)
{
    connector_->SetNewConnectionCallback(
        std::bind(&TcpClient::NewConnection, this, std::placeholders::_1));
}

TcpClient::~TcpClient()
{
    TcpConnectionPtr conn;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        conn = connection_;
    }

    if (conn)
    {
        // 确保连接在所属的 loop 中被安全关闭
        auto cb = std::bind(&TcpConnection::ForceClose, conn);
        loop_->RunInLoop(cb);
    }
    else
    {
        connector_->Stop();
    }
}

void TcpClient::Connect()
{
    connect_ = true;
    connector_->Start();
}

void TcpClient::Disconnect() 
{
    connect_ = false;
    std::lock_guard<std::mutex> lock(mutex_);
    if (connection_)
    {
        connection_->ShutDown(); // 优雅关闭写端
    }
}

void TcpClient::Stop()
{
    connect_ = false;
    connector_->Stop();
}

void TcpClient::NewConnection(int sockfd)
{
    loop_->AssertInLoopThread();

    TcpConnectionPtr conn = std::make_shared<TcpConnection>(loop_, sockfd);

    // 设置回调
    conn->SetConnectionCallback(connection_callback_);
    conn->SetMessageCallback(message_callback_);
    conn->SetWriteCompleteCallback(write_complete_callback_);

    // 断开清理的回调
    conn->SetCloseCallback(std::bind(&TcpClient::RemoveConnection, this, _1));

    {
        std::lock_guard<std::mutex> lock(mutex_);
        connection_ = conn;
    }

    connection_->ConnectEstablished();
}

void TcpClient::RemoveConnection(const TcpConnectionPtr& conn)
{
    loop_->AssertInLoopThread();

    ThreadSwitcher::Run(conn->GetLoop(), conn, &TcpConnection::ConnectDestroyed);

    {
        std::lock_guard<std::mutex> lock(mutex_);
        connection_.reset();
    }

    // 检查是否是超时断开，如果不是业务端的连接断开，自动重新连接
    if (retry_ && connect_)
    {
        LOG_INFO << "TcpClient::RemoveConnection - Reconnecting to " << connector_->GetIp() << " " << connector_->GetPort();
        // 让工兵 Connector 重新挂到 epoll 上去发起非阻塞 connect！
        connector_->Start();
    }
}
```





## CMake
```cpp
cmake_minimum_required(VERSION 3.0)
project(MyMuduo)

include_directories(/usr/include)
include_directories(/usr/local/include)
include_directories(/usr/local/include/hiredis)

link_directories(/usr/local/lib)

# 1. 设置编译选项（调试模式）
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g")

# 2. 指定输出路径（对应你图中的 bin 目录）
set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin)
set(LIBRARY_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/lib)

# 项目编译依赖库搜索路径
link_directories(${PROJECT_SOURCE_DIR}/lib)

# 3. 关键：进入子目录执行
# 这样 CMake 就会去 src 目录下找另一个 CMakeLists.txt
add_subdirectory(src)
```



```cpp
add_compile_options(-fsanitize=address -g)
add_link_options(-fsanitize=address)

# 头文件
include_directories(${PROJECT_SOURCE_DIR}/include)
include_directories(${PROJECT_SOURCE_DIR}/src/include/base)
include_directories(${PROTO_SRC_DIR})

#源文件
aux_source_directory(${PROJECT_SOURCE_DIR}/src MAIN_SRC)
aux_source_directory(${PROJECT_SOURCE_DIR}/src/Log LOG_SRC)
aux_source_directory(${PROJECT_SOURCE_DIR}/src/net NET_SRC)
set(BASE_SRC ${LOG_SRC} ${NET_SRC})

# 可执行文件
add_executable(muduo ${MAIN_SRC} ${BASE_SRC})
target_link_libraries(muduo pthread muduo_net muduo_base mysqlcppconn hiredis protobuf zookeeper_mt)

# 测试文件
add_executable(test ${PROJECT_SOURCE_DIR}/src/test/test_event_loop.cpp ${BASE_SRC})
target_link_libraries(test pthread muduo_net muduo_base mysqlcppconn hiredis protobuf zookeeper_mt)

# 测试回声server
add_executable(test_echo_server ${PROJECT_SOURCE_DIR}/src/test/test_echo_server.cpp ${BASE_SRC})
target_link_libraries(test pthread muduo_net muduo_base mysqlcppconn hiredis protobuf zookeeper_mt)

# 测试http server
add_executable(test_echo_http_server ${PROJECT_SOURCE_DIR}/src/test/test_echo_http_server.cpp ${BASE_SRC})
target_link_libraries(test pthread muduo_net muduo_base mysqlcppconn hiredis protobuf zookeeper_mt)

# 测试事件
add_executable(test_timer ${PROJECT_SOURCE_DIR}/src/test/test_timer.cpp ${BASE_SRC})
target_link_libraries(test pthread muduo_net muduo_base mysqlcppconn hiredis protobuf zookeeper_mt)
```



## GitHub
1. 添加.gitignore文件

来声明哪些文件可以上传，哪些不能



2. 到文件夹下面创建版本库

<!-- 这是一张图片，ocr 内容为： -->
![](https://cdn.nlark.com/yuque/0/2026/png/54962239/1773223436050-f84aae5d-1c65-4948-bf37-30aba7e40a3c.png)



3. 然后打开Git设置

添加远端为origin，以及仓库的地址

<!-- 这是一张图片，ocr 内容为： -->
![](https://cdn.nlark.com/yuque/0/2026/png/54962239/1773223533240-c3a7a909-6331-43fb-83b1-61ae2af9f315.png)



4. 右键推送

<!-- 这是一张图片，ocr 内容为： -->
![](https://cdn.nlark.com/yuque/0/2026/png/54962239/1773223597590-48a69170-5d76-4ddc-a09b-1c300f520c9e.png)



## 生产环境
在这里预设生产环境中可能遇到的问题





## 优化
<!-- 这是一张图片，ocr 内容为： -->
![](https://cdn.nlark.com/yuque/0/2026/png/54962239/1773310422177-cd4a04b7-dd61-40cb-8673-2a934e6fe4eb.png)



