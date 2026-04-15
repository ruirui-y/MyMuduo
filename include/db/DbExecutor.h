#ifndef DB_EXECUTOR_H
#define DB_EXECUTOR_H

#include "DbTypes.h"
#include <functional>
#include <string>
#include <vector>

class EventLoop;
class ThreadPool;

// 提前声明 MySQL 的接口类
namespace sql {
    class PreparedStatement;
    class ResultSet;
    class ResultSetMetaData;
}

class DbExecutor {
public:
    using QueryCallback = std::function<void(const DbResultSet&)>;              // 查询回调
    using UpdateCallback = std::function<void(int affectedRows)>;               // 插入或者删除回调
    using TransactionCallback = std::function<void(bool success)>;              // 事务回调：成功或失败

    // =========================================================
    // 异步查询 (SELECT 语句)
    // =========================================================
    static void AsyncQuery(EventLoop* loop, ThreadPool* threadPool,
        const std::string& sql, const DbParams& params, QueryCallback cb);

    // =========================================================
    // 异步更新 (INSERT / UPDATE / DELETE 语句)
    // =========================================================
    static void AsyncUpdate(EventLoop* loop, ThreadPool* threadPool,
        const std::string& sql, const DbParams& params, UpdateCallback cb);

    // =========================================================
    // 异步事务 (保证多条 SQL 原子性)
    // =========================================================
    static void AsyncTransaction(EventLoop* loop, ThreadPool* threadPool,
        const std::vector<std::string>& sqls,
        const std::vector<DbParams>& allParams,
        TransactionCallback cb);

private:
    // =========================================================
    // 内部辅助函数
    // =========================================================
    static void bindParams(sql::PreparedStatement* pstmt, const DbParams& params);
    static DbValue extractDbValue(sql::ResultSet* res, sql::ResultSetMetaData* meta, int index);
};

#endif // DB_EXECUTOR_H