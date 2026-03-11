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