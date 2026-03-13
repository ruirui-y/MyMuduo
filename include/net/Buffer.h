#ifndef BUFFER_H
#define BUFFER_H

#include <vector>
#include <string>
#include <algorithm>
#include <sys/uio.h>
#include <arpa/inet.h>
#include <cstring>


/// A buffer class modeled after org.jboss.netty.buffer.ChannelBuffer
///
/// @code
/// +-------------------+------------------+------------------+
/// | prependable bytes |  readable bytes  |  writable bytes  |
/// |                   |     (CONTENT)    |                  |
/// +-------------------+------------------+------------------+
/// |                   |                  |                  |
/// 0      <=      readerIndex   <=   writerIndex    <=     size
/// @endcode

class Buffer
{
public:
	static const size_t kCheapPrepend = 8;									// 预留头部空间
	static const size_t kInitialSize = 1024;

	Buffer() : buffer_(kCheapPrepend + kInitialSize), read_index_(kCheapPrepend), write_index_(kCheapPrepend) {}

	size_t ReadableBytes() const { return write_index_ - read_index_; }		// 可读字节数
	size_t WriteableBytes() const { return buffer_.size() - write_index_; }	// 可写字节数

	// 添加char数组
	void Append(const char* data, size_t len)
	{
		if (WriteableBytes() < len)
		{
			MakeSpace(len);
		}

		std::copy(data, data + len, begin() + write_index_);
		write_index_ += len;
	}

	// 追加int
	void AppendInt32(int32_t x)
	{
		int32_t be32 = htonl(x);											// 转换成网络字节序
		Append(static_cast<const char*>(static_cast<void*>(&be32)), sizeof(be32));
	}

	// 查询当前位置后4字节int数据，并不读取
	int32_t PeekInt32() const
	{
		int32_t be32 = 0;
		::memcpy(&be32, peek(), sizeof(be32));
		return ntohl(be32);													// 转回主机字节序
	}

	// 读取 32 位整数 (移动读指针，用于剥离包头)
	int32_t RetrieveInt32()
	{
		int32_t result = PeekInt32();
		retrieve(sizeof(int32_t));
		return result;
	}

	const char* peek() const { return begin() + read_index_; }				// 获取当前读指针
	void retrieve(size_t len) { read_index_ += len; }
	char* beginWrite() { return begin() + write_index_; }

	std::string RetrieveAllAsString() 
	{
		return RetrieveAsString(ReadableBytes());
	}

	std::string RetrieveAsString(size_t len) 
	{
		std::string result(peek(), len);
		retrieve(len);														// 将 readIndex_ 前移 len
		return result;
	}

	size_t PrependableBytes() const { return read_index_ - kCheapPrepend; }	// 可以回收的字节数

	// 自动扩容
	void MakeSpace(size_t len)
	{
		// 如果当前剩余空间加上可以回收的前向空间的大小小于需要写入的数据，则进行扩容
		if (WriteableBytes() + PrependableBytes() < len + kCheapPrepend)
		{
			buffer_.resize(write_index_ + len);
		}
		else
		{
			// 将数据从 read_index_ 复制到 kCheapPrepend 处，直接覆盖，也就是从后面read_index_处将数据挪到开头
			size_t read_able = ReadableBytes();
			std::copy(begin() + read_index_, begin() + write_index_, begin() + kCheapPrepend);
			read_index_ = kCheapPrepend;
			write_index_ = read_index_ + read_able;
		}
	}

	// 直接从缓冲区上读数据
	ssize_t ReadFd(int fd, int* save_errno)
	{
		char extra_buf[65536];
		struct iovec vec[2];
		size_t write_able = WriteableBytes();

		vec[0].iov_base = begin() + write_index_;
		vec[0].iov_len = write_able;
		vec[1].iov_base = extra_buf;
		vec[1].iov_len = sizeof extra_buf;

		// readv实现一次调用读入两块内存
		const ssize_t n = ::readv(fd, vec, 2);
		if (n < 0)
		{
			*save_errno = n;
		}
		else if(static_cast<size_t>(n) <= write_able)
		{
			// 场景 A：数据全部塞进了 Buffer 的剩余空间。
			// 此时 vec[1] 完全没用到，直接更新 write_index_ 即可
			write_index_ += n;
		}
		else
		{
			// 场景 B：数据超过了 Buffer 的剩余空间。
			// 1. 先把 Buffer 填满：
			write_index_ = buffer_.size();

			// 2. 把剩下的数据塞入 Buffer：
			// n - write_able 正是 extra_buf 里实际读到的字节数。
			// Append 内部会触发 MakeSpace，进行扩容或内存挪动。
			Append(extra_buf, n - write_able);
		}
		return n;
	}

private:
	char* begin() { return &*buffer_.begin(); }
	const char* begin() const { return &*buffer_.begin(); }

private:
	std::vector<char> buffer_;
	size_t read_index_;
	size_t write_index_;
};

#endif