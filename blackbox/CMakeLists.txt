# ESP32 Blackbox 项目根 CMake 构建配置文件
# 设置 CMake 最低版本要求，引入 ESP-IDF 项目构建系统

# 设置 CMake 最低版本为 3.16（ESP-IDF v4.4+ 要求）
cmake_minimum_required(VERSION 3.16)

# 引入 ESP-IDF 项目构建系统
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# 定义项目名称为 "esp32-blackbox"
# Git Bash（MSys）下 idf_tools.py 入口直接拒绝执行（MSYSTEM 由 MSYS 运行时
# 注入原生子进程，unset 无效），跳过 Python 依赖预检（同 s3zero 项目）。
idf_build_set_property(__CHECK_PYTHON 0)

project(esp32-blackbox)
