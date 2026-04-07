# ==========================================
# 1. 收集核心源文件 (不要把包含 main 函数的文件放进来！)
# ==========================================
aux_source_directory(${PROJECT_SOURCE_DIR}/src/Log LOG_SRC)
aux_source_directory(${PROJECT_SOURCE_DIR}/src/net NET_SRC)
aux_source_directory(${PROJECT_SOURCE_DIR}/src/base BASE_SRC)
set(SRC ${LOG_SRC} ${NET_SRC} ${BASE_SRC})

# ==========================================
# 2. 生成静态库：libmymuduo.a
# ==========================================
add_library(mymuduo STATIC ${SRC})

# 【关键魔法】告诉 CMake：凡是链接了 mymuduo 的程序，自动包含以下头文件路径！
target_include_directories(mymuduo PUBLIC 
    ${PROJECT_SOURCE_DIR}/include 
    ${PROJECT_SOURCE_DIR}/src/include/base
)

# 链接第三方依赖库 
# 注意：因为你自己写了 Muduo，所以我把你原本写的 muduo_net 和 muduo_base 删掉了，不要去链接原版的库！
target_link_libraries(mymuduo PUBLIC pthread)

# ==========================================
# 3. 编译你的测试程序 (直接链接静态库)
# ==========================================

# 测试 EventLoop
add_executable(test_event_loop ${PROJECT_SOURCE_DIR}/src/test/test_event_loop.cpp)
# 直接链接 mymuduo 即可，不需要再加 ${BASE_SRC}
target_link_libraries(test_event_loop PRIVATE mymuduo)

# 测试回声 Server
add_executable(test_echo_server ${PROJECT_SOURCE_DIR}/src/test/test_echo_server.cpp)
target_link_libraries(test_echo_server PRIVATE mymuduo)

# 测试 HTTP Server
add_executable(test_echo_http_server ${PROJECT_SOURCE_DIR}/src/test/test_echo_http_server.cpp)
target_link_libraries(test_echo_http_server PRIVATE mymuduo)

# 测试 Timer
add_executable(test_timer ${PROJECT_SOURCE_DIR}/src/test/test_timer.cpp)
target_link_libraries(test_timer PRIVATE mymuduo)

# ==========================================
# 4. (可选) 安装规则：供别人使用
# ==========================================
# 执行 sudo make install 时，会把静态库和头文件拷贝到系统目录
install(TARGETS mymuduo ARCHIVE DESTINATION lib)
install(DIRECTORY ${PROJECT_SOURCE_DIR}/include/ DESTINATION include/mymuduo FILES_MATCHING PATTERN "*.h")