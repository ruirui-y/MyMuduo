#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <vector>
#include <queue>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <string>

class ThreadPool
{
public:
	// 任务类型
	using Task = std::function<void()>;

	explicit ThreadPool(const std::string& nameArg = std::string("ThreadPool"));
	~ThreadPool();

	// 线程池的启动和停止
	void start(int num_threads);
	void stop();

	// 投递任务
	void run(Task task);

private:
	void runInThread();

private:
	std::mutex mutex_;
	std::condition_variable cond_;
	std::string name_;
	std::vector<std::unique_ptr<std::thread>> threads_;
	std::queue<Task> queue_;
	bool running_;
};

#endif