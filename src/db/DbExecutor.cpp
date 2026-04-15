#include "DbExecutor.h"

#include "ConnectionPool.h"
#include "ThreadPool.h"
#include "net/EventLoop.h"                                              
#include "Log/Logger.h"

#include <cppconn/prepared_statement.h>
#include <cppconn/resultset.h>
#include <cppconn/datatype.h>
#include <memory>

void DbExecutor::AsyncQuery(EventLoop* loop, ThreadPool* threadPool,
    const std::string& sql, const DbParams& params, QueryCallback cb)
{
    // 1. 打包丢进后台 DB 线程池执行阻塞任务
    threadPool->run([loop, sql, params, cb]()
        {
            DbResultSet results;
            auto conn = ConnectionPool::Instance().GetConnection(); // 注意大小写：根据你 ConnectionPool 的实现可能是 getConnection 

            if (conn)
            {
                ConnectionGuard guard(conn); // RAII 护盾，保证退出时归还连接
                try {
                    std::unique_ptr<sql::PreparedStatement> pstmt(conn->prepareStatement(sql));
                    bindParams(pstmt.get(), params);

                    // sql::ResultSet 内部维护一个游标，游标默认在第一行之前，next(),游标++;
                    std::unique_ptr<sql::ResultSet> res(pstmt->executeQuery());

                    sql::ResultSetMetaData* meta = res->getMetaData();                                          // 获取元数据
                    int colCount = meta->getColumnCount();                                                      // 获取当前查询结果有多少列

                    // 遍历每一行
                    while (res->next())
                    {
                        DbRecord record;                                                                        // 存储每一行的数据

                        // 遍历每一列
                        for (int i = 1; i <= colCount; ++i)
                        {
                            // 获取每一列的列名
                            std::string colName = meta->getColumnLabel(i);
                            record[colName] = extractDbValue(res.get(), meta, i);
                        }
                        results.push_back(std::move(record));                                                   // 存储查询到的所有行的数据
                    }
                }
                catch (sql::SQLException& e) {
                    LOG_ERROR << "SQL Query Error: " << e.what()
                        << " | SQL: " << sql;
                }
            }
            else {
                LOG_ERROR << "Failed to get connection from pool for query: " << sql;
            }

            // 2. 跨线程跳跃：带着数据回到主网络线程触发回调
            if (cb && loop) {
                loop->RunInLoop([cb, results]() {
                    cb(results);
                    });
            }
        });
}

void DbExecutor::AsyncUpdate(EventLoop* loop, ThreadPool* threadPool,
    const std::string& sql, const DbParams& params, UpdateCallback cb)
{
    threadPool->run([loop, sql, params, cb]() {
        int affectedRows = -1;
        auto conn = ConnectionPool::Instance().GetConnection();

        if (conn) {
            ConnectionGuard guard(conn);
            try {
                std::unique_ptr<sql::PreparedStatement> pstmt(conn->prepareStatement(sql));
                bindParams(pstmt.get(), params);
                affectedRows = pstmt->executeUpdate();
            }
            catch (sql::SQLException& e) {
                LOG_ERROR << "SQL Update Error: " << e.what()
                    << " | SQL: " << sql;
            }
        }
        else {
            LOG_ERROR << "Failed to get connection from pool for update: " << sql;
        }

        // 执行完毕，回到主网络线程通知结果
        if (cb && loop) {
            loop->RunInLoop([cb, affectedRows]() {
                cb(affectedRows);
                });
        }
        });
}

void DbExecutor::AsyncTransaction(EventLoop* loop, ThreadPool* threadPool,
    const std::vector<std::string>& sqls,
    const std::vector<DbParams>& allParams,
    TransactionCallback cb)
{
    threadPool->run([loop, sqls, allParams, cb]()
        {
            bool success = false;
            auto conn = ConnectionPool::Instance().GetConnection();

            if (conn)
            {
                ConnectionGuard guard(conn);
                try
                {
                    // 1. 关闭自动提交，开启事务屏障
                    conn->setAutoCommit(false);

                    // 2. 批量执行 SQL
                    for (size_t i = 0; i < sqls.size(); ++i) {
                        const std::string& sql = sqls[i];
                        std::unique_ptr<sql::PreparedStatement> pstmt(conn->prepareStatement(sql));

                        // 如果这条 SQL 带有参数，则绑定参数
                        if (i < allParams.size() && !allParams[i].empty()) {
                            bindParams(pstmt.get(), allParams[i]);
                        }
                        pstmt->executeUpdate();
                    }

                    // 3. 完美执行，提交事务
                    conn->commit();
                    success = true;
                    LOG_INFO << "Transaction committed successfully. Executed SQL count: " << sqls.size();
                }
                catch (sql::SQLException& e)
                {
                    LOG_ERROR << "MySQL Transaction Error: " << e.what() << ". Initiating Rollback...";
                    // 防御性编程：防止 Rollback 自身抛出异常导致跳过后续的清理代码
                    try { conn->rollback(); }
                    catch (...) { LOG_ERROR << "Rollback failed FATALLY!"; }
                }
                catch (std::exception& e)
                {
                    LOG_ERROR << "Standard Exception in Transaction: " << e.what() << ". Initiating Rollback...";
                    try { conn->rollback(); }
                    catch (...) { LOG_ERROR << "Rollback failed FATALLY!"; }
                }

                // 4. 环境复原：无论成功还是爆炸，必须恢复自动提交！
                try {
                    conn->setAutoCommit(true);
                }
                catch (...) {
                    LOG_ERROR << "Failed to restore auto-commit state! This connection might be poisoned.";
                }
            }
            else {
                LOG_ERROR << "Failed to get connection from pool for transaction.";
            }

            // 5. 跨线程回到网络层报告战况
            if (cb && loop) {
                loop->RunInLoop([cb, success]() { cb(success); });
            }
        });
}

void DbExecutor::bindParams(sql::PreparedStatement* pstmt, const DbParams& params) {
    for (size_t i = 0; i < params.size(); ++i) {
        int index = i + 1; // PreparedStatement 索引从 1 开始
        const auto& val = params[i];

        if (std::holds_alternative<std::monostate>(val)) {
            pstmt->setNull(index, 0);
        }
        else if (std::holds_alternative<int>(val)) {
            pstmt->setInt(index, std::get<int>(val));
        }
        else if (std::holds_alternative<std::string>(val)) {
            pstmt->setString(index, std::get<std::string>(val));
        }
        else if (std::holds_alternative<bool>(val)) {
            pstmt->setBoolean(index, std::get<bool>(val));
        }
        else if (std::holds_alternative<double>(val)) {
            pstmt->setDouble(index, std::get<double>(val));
        }
    }
}

DbValue DbExecutor::extractDbValue(sql::ResultSet* res, sql::ResultSetMetaData* meta, int index) {
    // 先判断是否为 NULL
    if (res->isNull(index)) {
        return std::monostate{};
    }

    int colType = meta->getColumnType(index);

    switch (colType) {
    case sql::DataType::BIT:
        return res->getBoolean(index);

    case sql::DataType::TINYINT:
    case sql::DataType::SMALLINT:
    case sql::DataType::INTEGER:
        return res->getInt(index);

    // 普通浮点数
    case sql::DataType::REAL:
    case sql::DataType::DOUBLE:
        return static_cast<double>(res->getDouble(index));

    // 高精度浮点数
    case sql::DataType::DECIMAL:
    case sql::DataType::NUMERIC:
    case sql::DataType::BIGINT:
        // 拦截 64 位整型，强制转为 string 防止溢出丢失精度
        return res->getString(index);

    case sql::DataType::CHAR:
    case sql::DataType::VARCHAR:
    case sql::DataType::LONGVARCHAR:
    case sql::DataType::TIMESTAMP:
    case sql::DataType::DATE:
    case sql::DataType::TIME:
    default:
        // 其他全部安全降级为 string
        return res->getString(index);
    }
}