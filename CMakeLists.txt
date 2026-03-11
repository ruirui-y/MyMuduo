cmake_minimum_required(VERSION 3.0)
project(MyMuduo)

# 1. 编译选项 (设置 C++17, 开启多线程, 以及调试信息)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -pthread")

# 开启 ASan 内存检测 (如果后续要发布 release 版本，可以把这两行注释掉)
add_compile_options(-fsanitize=address -g)
add_link_options(-fsanitize=address)

# 2. 全局头文件搜索路径 (第三方库)
include_directories(/usr/include)
include_directories(/usr/local/include)
include_directories(/usr/local/include/hiredis)

link_directories(/usr/local/lib)

# 3. 指定输出路径：可执行文件在 bin，静态库在 lib
set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin)
set(LIBRARY_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/lib)

# 4. 进入子目录处理核心逻辑
add_subdirectory(src)