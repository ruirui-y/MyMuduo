#!/bin/bash

# 遇到任何错误立即终止脚本运行，防止环境装了一半继续往下跑
set -e

echo "=========================================================="
echo "🚀 开始初始化 MyMuduo 及其相关业务模块的编译环境..."
echo "=========================================================="

echo "[1/4] 更新系统软件源..."
sudo apt update

echo "[2/4] 安装核心编译工具链 (C++17, CMake, GDB)..."
sudo apt install -y build-essential cmake gdb

echo "[3/4] 安装底层网络与存储依赖 (OpenSSL, MySQL, Redis)..."
sudo apt install -y libssl-dev
sudo apt install -y libmysqlcppconn-dev
sudo apt install -y libhiredis-dev

echo "[4/4] 安装序列化与数据格式化库 (Protobuf, Json)..."
sudo apt install -y libprotobuf-dev protobuf-compiler
# nlohmann-json3-dev 安装后，头文件会自动放入 /usr/include/nlohmann/json.hpp
sudo apt install -y nlohmann-json3-dev

echo "=========================================================="
echo "🎉 所有环境依赖已安装完毕！"
echo "👉 下一步，您可以直接编译项目："
echo "   mkdir build && cd build"
echo "   cmake .. && make -j4"
echo "   sudo make install"
echo "=========================================================="