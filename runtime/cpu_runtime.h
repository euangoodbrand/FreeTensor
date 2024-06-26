#ifndef FREE_TENSOR_CPU_RUNTIME_H
#define FREE_TENSOR_CPU_RUNTIME_H

#include <algorithm> // min, max
#include <array>     // ByValue
#include <atomic>
#include <cassert>
#include <cmath> // INFINITY, sqrt, exp
#include <cstdint>
#include <type_traits>

#include <omp.h>

#ifdef FT_WITH_MKL
#include <mkl.h>
#endif

#include "cpu_context.h"
#include "mdspan.h"
#include "unchecked_opt.h"

#include "../3rd-party/half/include/half.hpp"

#define restrict __restrict__
#define __ByValArray std::array

template <typename T>
concept IntegralExceptBool = requires {
    requires std::integral<T>;
    requires !std::same_as<T, bool>;
};

inline auto floorDiv(IntegralExceptBool auto a, IntegralExceptBool auto b) {
    auto res = a / b;
    auto rem = a % b;
    return res - (rem != 0 && ((rem < 0) != (b < 0)));
}

inline auto ceilDiv(IntegralExceptBool auto a, IntegralExceptBool auto b) {
    auto res = a / b;
    auto rem = a % b;
    return res + (rem != 0 && ((rem < 0) == (b < 0)));
}

inline auto runtime_mod(IntegralExceptBool auto a, IntegralExceptBool auto b) {
    auto m = a % b;
    if ((m > 0 && b < 0) || (m < 0 && b > 0)) {
        m += b;
    }
    return m;
}

template <class T> T runtime_square(T x) { return x * x; }

template <class T> T runtime_sigmoid(T x) { return 1.0 / (1.0 + std::exp(-x)); }

template <class T> void atomicUpdate(T &x, auto &&update) {
    // No need to keep the life time of `std::atomic_ref` outside this function:
    // Atomic operations applied to an object through an `std::atomic_ref` are
    // atomic with respect to atomic operations applied through any other
    // `std::atomic_ref` referencing the same object.
    std::atomic_ref<T> xAtomic(x);

    T xOld = xAtomic, y;
    do {
        y = update(xOld);
    } while (
        !xAtomic.compare_exchange_weak(xOld, y, std::memory_order_relaxed));
    // - `_weak` means we may fail even if `x` is unchanged, and we retry
    // - We can use a relaxed memory order: Since an `atomicUpdate` only
    // competes with other `atomicUpdate`s (FreeTensor's schedule ensures there
    // is no simultaneous `Load` and `ReduceTo` or simultaneous `Store` and
    // `ReduceTo`), and the only memory access in the loop of `atomicUpdate` is
    // this `compare_exchange`, we don't need to worry about the relative order
    // of this access with other accesses that cause side effect
}

#endif // FREE_TENSOR_CPU_RUNTIME_H
