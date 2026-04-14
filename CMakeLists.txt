# ==============================================================================
# MyMuduo 核心网络库构建系统 (Root CMakeLists)
# ==============================================================================

cmake_minimum_required(VERSION 3.0)
project(MyMuduo)

# ==========================================
# 1. 全局编译选项配置
# ==========================================
# 强制使用 C++17 标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 核心编译参数: 
# -g: 生成调试信息 
# -pthread: 开启多线程支持 
# -fno-omit-frame-pointer: 保留帧指针(方便宕机时看崩溃调用栈，工业级必备)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -pthread -fno-omit-frame-pointer")

# 【性能与安全调试装甲】内存泄漏/越界检测 (ASan)
# 架构师注：平时开发强烈建议打开！发布 Release 版本时注释掉即可。
# add_compile_options(-fsanitize=address -g)
# add_link_options(-fsanitize=address)

# ==========================================
# 2. 全局第三方依赖搜索路径 (System/Libs)
# ==========================================
# 头文件搜索路径
include_directories(/usr/include)
include_directories(/usr/local/include)
include_directories(/usr/local/include/hiredis) # Redis 客户端

# 动态库/静态库搜索路径
link_directories(/usr/local/lib)
link_directories(/usr/lib64)

# ==========================================
# 3. 全局输出路径路由
# ==========================================
# 把所有的可执行文件(测试程序)统一扔到顶层的 bin 目录
set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin)

# 把生成的 libmymuduo.a 静态库统一扔到顶层的 lib 目录
set(LIBRARY_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/lib)

# ==========================================
# 4. 委派构建任务
# ==========================================
# 进入 src 目录，执行里面的 CMakeLists.txt
add_subdirectory(src)