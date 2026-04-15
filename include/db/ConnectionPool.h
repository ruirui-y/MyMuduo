#ifndef CONNECTION_POOL_H
#define CONNECTION_POOL_H

#include <string>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <memory>
#include <cppconn/driver.h>
#include <cppconn/connection.h>
#include <cppconn/exception.h>

class ConnectionPool {
public:
    // 局部静态变量实现线程安全的单例
    static ConnectionPool& Instance() {
        static ConnectionPool instance;
        return instance;
    }

    // 初始化连接池
    bool Init(const std::string& host, const std::string& user,
        const std::string& pwd, const std::string& dbName, int poolSize = 5);

    // 阻塞获取连接（如果池子空了会等待）
    std::shared_ptr<sql::Connection> GetConnection();

    // 归还连接
    void ReturnConnection(std::shared_ptr<sql::Connection> conn);

private:
    ConnectionPool() = default;
    ~ConnectionPool();

    std::shared_ptr<sql::Connection> CreateNewConnection();

private:
    std::string host_, user_, password_, database_;
    sql::Driver* driver_ = nullptr;

    std::queue<std::shared_ptr<sql::Connection>> pool_;
    std::mutex mutex_;
    std::condition_variable cond_;
    int max_size_ = 0;
    bool initialized_ = false;
};

// RAII 连接护盾：离开作用域自动归还连接
class ConnectionGuard {
public:
    ConnectionGuard(std::shared_ptr<sql::Connection> conn) : conn_(conn) {}
    ~ConnectionGuard() {
        if (conn_) {
            ConnectionPool::Instance().ReturnConnection(conn_);
        }
    }

    // 封死拷贝和复制构造
    ConnectionGuard(const ConnectionGuard&) = delete;
    ConnectionGuard& operator=(const ConnectionGuard&) = delete;

private:
    std::shared_ptr<sql::Connection> conn_;
};

#endif // CONNECTION_POOL_H