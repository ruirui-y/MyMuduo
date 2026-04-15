#include "ConnectionPool.h"
#include "Log/Logger.h"

ConnectionPool::~ConnectionPool() {
    std::lock_guard<std::mutex> lock(mutex_);
    while (!pool_.empty()) {
        pool_.pop();                                                                // shared_ptr 的 deleter 会自动触发 close() 释放连接
    }
}

bool ConnectionPool::Init(const std::string& host, const std::string& user,
    const std::string& pwd, const std::string& dbName, int poolSize) {
    if (initialized_) return true;

    host_ = host;
    user_ = user;
    password_ = pwd;
    database_ = dbName;
    max_size_ = poolSize;

    driver_ = get_driver_instance();
    if (!driver_) {
        LOG_ERROR << "MySQL driver instance is null!";
        return false;
    }

    for (int i = 0; i < poolSize; ++i) {
        auto conn = CreateNewConnection();
        if (conn) pool_.push(conn);
    }

    initialized_ = !pool_.empty();
    if (initialized_) {
        LOG_INFO << "MySQL connection pool initialized successfully with " << pool_.size() << " connections.";
    }
    else {
        LOG_ERROR << "Failed to initialize MySQL connection pool!";
    }

    return initialized_;
}

std::shared_ptr<sql::Connection> ConnectionPool::CreateNewConnection() {
    try {
        sql::Connection* rawConn = driver_->connect(host_, user_, password_);
        rawConn->setSchema(database_);

        // 设置 shared_ptr 自定义删除器，自动清理底层连接
        return std::shared_ptr<sql::Connection>(rawConn, [](sql::Connection* conn) 
            {
                if (conn) {
                    conn->close();
                    delete conn;
                }
            });
    }
    catch (sql::SQLException& e) {
        // 使用异步日志记录错误
        LOG_ERROR << "DB Connection Error: " << e.what()
            << ", Error code: " << e.getErrorCode()
            << ", SQLState: " << e.getSQLState();
        return nullptr;
    }
}

std::shared_ptr<sql::Connection> ConnectionPool::GetConnection()
{
    std::unique_lock<std::mutex> lock(mutex_);

    // 如果池子空了，当前线程休眠死等，直到别人调用 returnConnection 唤醒它
    cond_.wait(lock, [this]() { return !pool_.empty(); });

    auto conn = pool_.front();
    pool_.pop();

    return conn;
}

void ConnectionPool::ReturnConnection(std::shared_ptr<sql::Connection> conn)
{
    if (!conn) return;
    std::lock_guard<std::mutex> lock(mutex_);
    pool_.push(conn);
    cond_.notify_one();                                                                 // 唤醒一个正在等待拿连接的线程
}