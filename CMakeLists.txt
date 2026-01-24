# CMakeLists.txt
cmake_minimum_required(VERSION 3.20) # cmake的最低版本要求
project(code_template_cmake CXX) # 项目名称 CXX表示C++项目

set(CMAKE_CXX_STANDARD_REQUIRED ON) # C++标准要求
set(CMAKE_CXX_STANDARD 20) # 设置C++标准版本

# 使用 o2 优化
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O2")

# 在配置阶段执行命令，将配置文件复制到build文件夹下
execute_process(COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/config.toml ${CMAKE_BINARY_DIR}/config.toml)

# 添加可执行文件
add_executable(main main.cpp)

# 添加包含目录
target_include_directories(main PRIVATE ${PROJECT_SOURCE_DIR}/include) # 添加头文件
