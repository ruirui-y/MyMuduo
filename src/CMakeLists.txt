# ==============================================================================
# MyMuduo 源码构建与测试模块 (Src CMakeLists)
# ==============================================================================

# ==========================================
# 1. 源码收集 (Source Collection)
# ==========================================
# 将不同子模块的 .cpp 文件分类收集到各自的变量中
aux_source_directory(${PROJECT_SOURCE_DIR}/src/Log  LOG_SRC)
aux_source_directory(${PROJECT_SOURCE_DIR}/src/net  NET_SRC)
aux_source_directory(${PROJECT_SOURCE_DIR}/src/base BASE_SRC)
aux_source_directory(${PROJECT_SOURCE_DIR}/src/db   DB_SRC)   # 新增的数据库模块

# 终极大合体：将所有模块源码融合成一个总集合
set(SRC ${LOG_SRC} ${NET_SRC} ${BASE_SRC} ${DB_SRC})

# ==========================================
# 2. 核心大建：生成 MyMuduo 静态库
# ==========================================
# 编译出一个名为 libmymuduo.a 的静态库
add_library(mymuduo STATIC ${SRC})

# 【Target 依赖魔法】
# 使用 PUBLIC 属性：告诉 CMake，不仅 mymuduo 自己编译时需要这些头文件，
# 任何链接了 mymuduo 的测试程序，也会自动继承这些头文件路径！(免去反复 include)
target_include_directories(mymuduo PUBLIC 
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/include/base
    ${PROJECT_SOURCE_DIR}/include/net
    ${PROJECT_SOURCE_DIR}/include/Log
    ${PROJECT_SOURCE_DIR}/include/db
)

# 链接第三方库依赖
# 极其关键：既然你加入了 db 模块，底层势必用到了 MySQL API！
# 这里必须加上 mysqlcppconn（或者 mysqlclient，取决于你用的哪个驱动），否则链接时必报未定义引用！
target_link_libraries(mymuduo PUBLIC 
    pthread 
    mysqlcppconn  # 架构师补丁：支持 ConnectionPool 和 SqlExec
    hiredis       # 如果你的 base/net 里用到了 redis，也放这里
)

# ==========================================
# 3. 兵工厂质检：编译所有的测试程序
# ==========================================
# 统一的宏函数：批量生成测试用例，保持 CMake 极度整洁
macro(add_mymuduo_test TEST_NAME FILE_PATH)
    add_executable(${TEST_NAME} ${FILE_PATH})
    # 只要链接了 mymuduo，它就会自动去上一节的头文件路径里找文件，极其优雅
    target_link_libraries(${TEST_NAME} PRIVATE mymuduo)
endmacro()

# 批量注册你的测试程序
add_mymuduo_test(test_event_loop       ${PROJECT_SOURCE_DIR}/src/test/test_event_loop.cpp)
add_mymuduo_test(test_echo_server      ${PROJECT_SOURCE_DIR}/src/test/test_echo_server.cpp)
add_mymuduo_test(test_echo_http_server ${PROJECT_SOURCE_DIR}/src/test/test_echo_http_server.cpp)
add_mymuduo_test(test_timer            ${PROJECT_SOURCE_DIR}/src/test/test_timer.cpp)

# ==========================================
# 4. 开源与发布 (Install Rules)
# ==========================================
# 执行 sudo make install 时的拷贝规则
# 1. 拷贝静态库到系统 lib 目录
install(TARGETS mymuduo ARCHIVE DESTINATION lib)

# 2. 拷贝你对外暴露的头文件到系统 include 目录 (假设你集中存放在顶层的 include 目录)
# 这样别人写代码就可以直接 #include <mymuduo/TcpServer.h> 了
install(DIRECTORY ${PROJECT_SOURCE_DIR}/include/ 
        DESTINATION include/mymuduo 
        FILES_MATCHING PATTERN "*.h"
)