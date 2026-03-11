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