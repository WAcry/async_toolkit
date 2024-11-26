enable_testing()

# 添加Google Test
include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG release-1.12.1
)
FetchContent_MakeAvailable(googletest)

# 添加测试可执行文件
add_executable(async_toolkit_tests
    task_pool_test.cpp
    pipeline_test.cpp
)

# 链接Google Test
target_link_libraries(async_toolkit_tests
    PRIVATE
    gtest
    gtest_main
)

# 添加测试
add_test(NAME async_toolkit_tests COMMAND async_toolkit_tests)
