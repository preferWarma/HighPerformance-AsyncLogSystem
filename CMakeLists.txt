# CMakeLists.txt
cmake_minimum_required(VERSION 3.20) # cmake的最低版本要求

project(code_template_cmake CXX) # 项目名称 CXX表示C++项目

set(CMAKE_CXX_STANDARD_REQUIRED ON) # C++标准要求
set(CMAKE_CXX_STANDARD 20) # 设置C++标准版本

add_subdirectory(test)
