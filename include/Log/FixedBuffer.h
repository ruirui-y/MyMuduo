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