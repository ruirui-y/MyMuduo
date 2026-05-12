#include "db/ConnectionPool.h"
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

    while (true) {
        // 1. 死等拿到一个连接名额
        cond_.wait(lock, [this]() { return !pool_.empty(); });

        auto conn = pool_.front();
        pool_.pop();

        // 2. 极其关键：提前解锁！
        // 因为接下来的 ping(心跳) 和 连接重建 需要消耗网络 IO 时间。
        // 解锁后，别的线程照样可以正常向池子 ReturnConnection，提高并发度！
        lock.unlock();

        bool is_valid = false;
        try {
            // 3. 检查连接是否存活
            // isClosed() 只是看本地有没有关掉，isValid() 会真正向 MySQL 发送探测包
            if (conn && !conn->isClosed() && conn->isValid()) {
                is_valid = true;
            }
        }
        catch (sql::SQLException& e) {
            // 如果底层 TCP 都断了，调用 isValid 可能会直接抛出异常
            is_valid = false;
        }

        if (is_valid) {
            return conn; // 连接是活的，完美返回
        }

        // ==========================================
        // 4. 捕获到“死连接”，触发断线重连
        // ==========================================
        LOG_WARN << "[ConnectionPool] 捕获失效连接(被MySQL踢出)，准备销毁并重建...";

        // 重新调用建连函数
        // 原来的 conn 会失去引用计数，触发你之前写的 [](sql::Connection* conn){ delete conn; } 释放内存
        auto new_conn = CreateNewConnection();

        if (new_conn) {
            LOG_INFO << "[ConnectionPool] 重建数据库连接成功，已满血复活并分配给业务！";
            return new_conn;
        }
        else {
            // 5. 如果重建失败（比如此时数据库真挂了）
            LOG_ERROR << "[ConnectionPool] 重建连接失败！将保留池名额，等待下一次尝试。";

            // 【架构师核心细节】: 必须把这个“坏连接”当做占位符塞回池子！
            // 否则名额就永久减 1，死几次整个连接池就干涸枯竭了。
            ReturnConnection(conn);

            // 睡 1 秒，防止数据库真挂了的时候，while 循环狂转把 CPU 跑满并刷爆日志
            std::this_thread::sleep_for(std::chrono::seconds(1));

            // 重新加锁，进入下一轮 while 死循环，直到拿到好连接为止
            lock.lock();
        }
    }
}

void ConnectionPool::ReturnConnection(std::shared_ptr<sql::Connection> conn)
{
    if (!conn) return;
    std::lock_guard<std::mutex> lock(mutex_);
    pool_.push(conn);
    cond_.notify_one();                                                                 // 唤醒一个正在等待拿连接的线程
}