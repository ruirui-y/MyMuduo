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