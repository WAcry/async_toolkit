cmake_minimum_required(VERSION 3.14)
project(cpp_async_toolkit VERSION 1.0.0 LANGUAGES CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Add compilation options
if(MSVC)
    add_compile_options(/W4 /WX)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Werror)
endif()

# Set include directory
include_directories(${PROJECT_SOURCE_DIR}/include)

# Add tests
enable_testing()
add_subdirectory(tests)

# Install configuration
install(DIRECTORY include/ DESTINATION include)
