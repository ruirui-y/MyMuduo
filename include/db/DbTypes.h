#ifndef DB_TYPES_H
#define DB_TYPES_H
#include <string>
#include <vector>
#include <unordered_map>
#include <variant>
#include <cstdint>

// 1. 替代 QVariant: 支持空值、整数、浮点数、布尔值和字符串
using DbValue = std::variant<std::monostate, int, int64_t, double, bool, std::string>;

// 2. 替代 QList<QVariant>: 用于存放 SQL 绑定的参数
using DbParams = std::vector<DbValue>;

// 3. 替代 QVariantMap: 代表数据库查出来的一行记录 (列名 -> 值)
using DbRecord = std::unordered_map<std::string, DbValue>;

// 4. 替代 QList<QVariantMap>: 代表整个查询结果集
using DbResultSet = std::vector<DbRecord>;

// 辅助工具：判断 DbValue 是否为空 (类似于 QVariant::isNull())
inline bool isDbNull(const DbValue& val) {
    return std::holds_alternative<std::monostate>(val);
}

#endif