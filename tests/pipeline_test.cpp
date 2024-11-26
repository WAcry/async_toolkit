#include <gtest/gtest.h>
#include <async_toolkit/pipeline.hpp>
#include <string>

using namespace async_toolkit;

TEST(Pipeline, BasicPipeline) {
    auto pipeline = Pipeline<int>::create()
        .then([](int x) { return x * 2; })
        .then([](int x) { return x + 1; });
    
    EXPECT_EQ(pipeline.process(20), 41);
}

TEST(Pipeline, TypeTransformation) {
    auto pipeline = Pipeline<int>::create()
        .then([](int x) { return std::to_string(x); })
        .then([](std::string x) { return "Number: " + x; });
    
    EXPECT_EQ(pipeline.process(42), "Number: 42");
}

TEST(Pipeline, EmptyPipeline) {
    auto pipeline = Pipeline<int>::create();
    EXPECT_EQ(pipeline.process(42), 42);
}

TEST(Pipeline, ComplexTransformation) {
    auto pipeline = Pipeline<std::string>::create()
        .then([](const std::string& s) { return s.length(); })
        .then([](size_t len) { return len * 2; })
        .then([](int x) { return x > 10; });
    
    EXPECT_TRUE(pipeline.process("Hello World"));
    EXPECT_FALSE(pipeline.process("Hi"));
}

TEST(ParallelPipeline, BasicParallel) {
    auto pipe1 = Pipeline<int>::create()
        .then([](int x) { return x * 2; });
    
    auto pipe2 = Pipeline<int>::create()
        .then([](int x) { return x + 1; });
    
    auto parallel_pipe = parallel(std::move(pipe1), std::move(pipe2));
    auto result = parallel_pipe.process(20);
    
    EXPECT_EQ(std::get<0>(result), 40);
    EXPECT_EQ(std::get<1>(result), 21);
}

TEST(ParallelPipeline, ComplexParallel) {
    auto number_pipe = Pipeline<int>::create()
        .then([](int x) { return x * 2; });
    
    auto string_pipe = Pipeline<int>::create()
        .then([](int x) { return std::to_string(x); })
        .then([](std::string x) { return "Number: " + x; });
    
    auto bool_pipe = Pipeline<int>::create()
        .then([](int x) { return x > 50; });
    
    auto parallel_pipe = parallel(
        std::move(number_pipe),
        std::move(string_pipe),
        std::move(bool_pipe)
    );
    
    auto result = parallel_pipe.process(42);
    
    EXPECT_EQ(std::get<0>(result), 84);
    EXPECT_EQ(std::get<1>(result), "Number: 42");
    EXPECT_FALSE(std::get<2>(result));
}
