#pragma once

#include <functional>
#include <type_traits>
#include <concepts>
#include <memory>
#include <utility>

namespace async_toolkit {

template<typename T>
class Pipeline {
public:
    using value_type = T;

    template<typename F>
    requires std::invocable<F, T>
    auto then(F&& func) && {
        using result_type = std::invoke_result_t<F, T>;
        return Pipeline<result_type>(
            [prev = std::move(processor_), f = std::forward<F>(func)]
            (auto&& input) mutable {
                return f(prev(std::forward<decltype(input)>(input)));
            }
        );
    }

    template<typename U>
    auto process(U&& input) {
        return processor_(std::forward<U>(input));
    }

    static auto create() {
        return Pipeline<T>([](T x) { return x; });
    }

private:
    template<typename F>
    explicit Pipeline(F&& func) : processor_(std::forward<F>(func)) {}

    std::function<T(T)> processor_;
};

// Helper function to create a pipeline
template<typename T>
auto make_pipeline() {
    return Pipeline<T>::create();
}

// Parallel pipeline executor
template<typename... Pipelines>
class ParallelPipeline {
public:
    explicit ParallelPipeline(Pipelines... pipes)
        : pipelines_(std::make_tuple(std::move(pipes)...)) {}

    template<typename T>
    auto process(T&& input) {
        return std::apply([&input](auto&&... pipes) {
            return std::make_tuple(
                pipes.process(input)...
            );
        }, pipelines_);
    }

private:
    std::tuple<Pipelines...> pipelines_;
};

// Helper function to create parallel pipeline
template<typename... Pipelines>
auto parallel(Pipelines&&... pipes) {
    return ParallelPipeline<std::remove_reference_t<Pipelines>...>(
        std::forward<Pipelines>(pipes)...
    );
}

} // namespace async_toolkit
