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