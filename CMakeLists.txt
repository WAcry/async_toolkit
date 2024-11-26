cmake_minimum_required(VERSION 3.14)
project(cpp_async_toolkit VERSION 1.0.0 LANGUAGES CXX)

# 设置C++标准
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 添加编译选项
if(MSVC)
    add_compile_options(/W4 /WX)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Werror)
endif()

# 设置头文件目录
include_directories(${PROJECT_SOURCE_DIR}/include)

# 添加测试
enable_testing()
add_subdirectory(tests)

# 安装配置
install(DIRECTORY include/ DESTINATION include)
