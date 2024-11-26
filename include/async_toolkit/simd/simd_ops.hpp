#pragma once

#include <cstdint>
#include <array>
#include <type_traits>

#if defined(_MSC_VER)
#include <intrin.h>
#elif defined(__GNUC__) || defined(__clang__)
#include <x86intrin.h>
#endif

namespace async_toolkit::simd {

// SIMD vector type
template<typename T, size_t N>
class alignas(sizeof(T) * N) Vector {
    static_assert(std::is_arithmetic_v<T>, "Vector type must be arithmetic");
    static_assert(N > 0 && (N & (N - 1)) == 0, "Vector size must be a power of 2");

public:
    using value_type = T;
    static constexpr size_t size = N;

    Vector() = default;
    
    explicit Vector(T value) {
        std::fill_n(data_, N, value);
    }

    Vector(const std::array<T, N>& arr) {
        std::copy(arr.begin(), arr.end(), data_);
    }

    T& operator[](size_t i) { return data_[i]; }
    const T& operator[](size_t i) const { return data_[i]; }

    // SIMD operations
    Vector& operator+=(const Vector& rhs) {
        if constexpr (std::is_same_v<T, float> && N == 4) {
            #if defined(__SSE__)
            __m128 a = _mm_load_ps(data_);
            __m128 b = _mm_load_ps(rhs.data_);
            _mm_store_ps(data_, _mm_add_ps(a, b));
            #else
            for (size_t i = 0; i < N; ++i) data_[i] += rhs.data_[i];
            #endif
        }
        else if constexpr (std::is_same_v<T, double> && N == 2) {
            #if defined(__SSE2__)
            __m128d a = _mm_load_pd(data_);
            __m128d b = _mm_load_pd(rhs.data_);
            _mm_store_pd(data_, _mm_add_pd(a, b));
            #else
            for (size_t i = 0; i < N; ++i) data_[i] += rhs.data_[i];
            #endif
        }
        else {
            for (size_t i = 0; i < N; ++i) data_[i] += rhs.data_[i];
        }
        return *this;
    }

    Vector& operator-=(const Vector& rhs) {
        if constexpr (std::is_same_v<T, float> && N == 4) {
            #if defined(__SSE__)
            __m128 a = _mm_load_ps(data_);
            __m128 b = _mm_load_ps(rhs.data_);
            _mm_store_ps(data_, _mm_sub_ps(a, b));
            #else
            for (size_t i = 0; i < N; ++i) data_[i] -= rhs.data_[i];
            #endif
        }
        else if constexpr (std::is_same_v<T, double> && N == 2) {
            #if defined(__SSE2__)
            __m128d a = _mm_load_pd(data_);
            __m128d b = _mm_load_pd(rhs.data_);
            _mm_store_pd(data_, _mm_sub_pd(a, b));
            #else
            for (size_t i = 0; i < N; ++i) data_[i] -= rhs.data_[i];
            #endif
        }
        else {
            for (size_t i = 0; i < N; ++i) data_[i] -= rhs.data_[i];
        }
        return *this;
    }

    Vector& operator*=(const Vector& rhs) {
        if constexpr (std::is_same_v<T, float> && N == 4) {
            #if defined(__SSE__)
            __m128 a = _mm_load_ps(data_);
            __m128 b = _mm_load_ps(rhs.data_);
            _mm_store_ps(data_, _mm_mul_ps(a, b));
            #else
            for (size_t i = 0; i < N; ++i) data_[i] *= rhs.data_[i];
            #endif
        }
        else if constexpr (std::is_same_v<T, double> && N == 2) {
            #if defined(__SSE2__)
            __m128d a = _mm_load_pd(data_);
            __m128d b = _mm_load_pd(rhs.data_);
            _mm_store_pd(data_, _mm_mul_pd(a, b));
            #else
            for (size_t i = 0; i < N; ++i) data_[i] *= rhs.data_[i];
            #endif
        }
        else {
            for (size_t i = 0; i < N; ++i) data_[i] *= rhs.data_[i];
        }
        return *this;
    }

    // Convert to array
    std::array<T, N> to_array() const {
        std::array<T, N> result;
        std::copy(data_, data_ + N, result.begin());
        return result;
    }

private:
    alignas(sizeof(T) * N) T data_[N];
};

// Common type aliases
using float4 = Vector<float, 4>;
using double2 = Vector<double, 2>;
using int4 = Vector<int32_t, 4>;

// SIMD operation functions
template<typename T, size_t N>
Vector<T, N> operator+(Vector<T, N> lhs, const Vector<T, N>& rhs) {
    lhs += rhs;
    return lhs;
}

template<typename T, size_t N>
Vector<T, N> operator-(Vector<T, N> lhs, const Vector<T, N>& rhs) {
    lhs -= rhs;
    return lhs;
}

template<typename T, size_t N>
Vector<T, N> operator*(Vector<T, N> lhs, const Vector<T, N>& rhs) {
    lhs *= rhs;
    return lhs;
}

// SIMD math functions
template<typename T, size_t N>
Vector<T, N> abs(const Vector<T, N>& v) {
    Vector<T, N> result;
    for (size_t i = 0; i < N; ++i) {
        result[i] = std::abs(v[i]);
    }
    return result;
}

template<typename T, size_t N>
T dot(const Vector<T, N>& a, const Vector<T, N>& b) {
    T sum = 0;
    for (size_t i = 0; i < N; ++i) {
        sum += a[i] * b[i];
    }
    return sum;
}

} // namespace async_toolkit::simd
