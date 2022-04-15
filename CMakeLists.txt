cmake_minimum_required(VERSION 3.16)

# 顶层项目信息
project(TRANSCOD)

# 启用C++20 请使用gcc11 g++11以上
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++20")


add_subdirectory(src)
