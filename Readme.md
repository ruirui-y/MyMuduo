# MyMuduo - 高性能 C++17 网络库

![C++17](https://img.shields.io/badge/C++-17-blue.svg)
![Build](https://img.shields.io/badge/build-CMake-brightgreen.svg)

`MyMuduo` 是一个基于 **Reactor 模式** 的现代 C++ 高性能网络库，参考开源网络库 `muduo` 的核心思想，采用 **C++17** 进行了重构，剔除了 Boost 依赖，完全使用 C++ 标准库实现。

本项目专为高并发服务器开发设计，单机稳定并发 QPS 可达 **4.7万 ~ 6.9万**。

## ✨ 核心特性

* **One Loop Per Thread 模型**：基于 `epoll` 的多路复用机制，主线程 Accept 新连接，并通过 Round-Robin 分发给 Sub-Reactor 线程池处理 I/O，实现高并发解耦。
* **无锁化异步任务调度 (ThreadSwitcher)**：自主设计跨线程任务投递机制，利用 `eventfd` 高效唤醒 EventLoop。结合 C++ Lambda 与 `std::shared_ptr`，解决高并发下的 Heap-Use-After-Free 问题。
* **高精度连接超时管理 (Timing Wheel)**：基于“时间轮”算法实现海量非活跃连接的惰性淘汰，时间复杂度 $O(1)$，解决万级并发定时器性能瓶颈。
* **应用层双缓冲 (Buffer)**：非阻塞 I/O 读写缓冲区，支持自动扩容与分散读 (`readv`)。
* **四级异步日志系统 (AsyncLogging)**：多缓冲机制（前端写与后端落盘分离），确保业务逻辑不被磁盘 I/O 阻塞。

## 🚀 性能压测 (Performance)

测试环境：Ubuntu / 4核 CPU / 本地回环网络
测试工具：`wrk`

* **测试命令**：`wrk -t4 -c1000 -d30s http://127.0.0.1:8888/`
* **压测结果**：在 1000 个高并发长连接下，稳定达到 **47,000+ QPS**，极速模式下峰值突破 **69,000 QPS**，无内存泄漏与段错误。

## 🛠️ 编译与安装

要求：CMake >= 3.14，支持 C++17。

```bash
git clone [https://github.com/ruirui-y/MyMuduo.git](https://github.com/ruirui-y/MyMuduo.git)
cd MyMuduo
mkdir build && cd build
cmake ..
make -j4
sudo make install
```