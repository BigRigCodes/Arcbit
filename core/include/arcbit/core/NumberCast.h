#pragma once

#include <arcbit/core/Types.h>

#include <cmath>
#include <limits>
#include <type_traits>
#include <utility>

namespace Arcbit
{
// True when `value` truncates to a representable integer T. NaN, infinity
// and out-of-range floats would make the cast undefined behavior. The bounds
// are powers of two (exact in floating point), so the upper check can't
// round up past T's max - which matters for i64 / u64, whose max isn't
// representable as a double.
template<typename T, typename F>
[[nodiscard]] bool FloatFitsInteger(const F value)
{
    if (!std::isfinite(value)) return false;
    const f64 v     = static_cast<f64>(value);
    const f64 lower = static_cast<f64>((std::numeric_limits<T>::min)()) - 1.0; // exclusive: truncation
    const f64 upper = std::ldexp(1.0, std::numeric_limits<T>::digits);       // max + 1, exclusive
    return v > lower && v < upper;
}

// Number-to-number conversion that never invokes undefined behavior:
// anything that isn't a number (bool counts as not one), or a value that
// doesn't fit in T - a negative into u32, NaN or 1e100 into i32, 2^63 into
// i64 - yields the fallback.
template<typename T, typename V>
[[nodiscard]] T ConvertNumber(const V& value, const T& fallback)
{
    constexpr bool numericT = std::is_arithmetic_v<T> && !std::is_same_v<T, bool>;
    constexpr bool numericV = std::is_arithmetic_v<V> && !std::is_same_v<V, bool>;
    if constexpr (numericT && numericV) {
        if constexpr (std::is_integral_v<T> && std::is_integral_v<V>)
            if (!std::in_range<T>(value)) return fallback;
        if constexpr (std::is_integral_v<T> && std::is_floating_point_v<V>)
            if (!FloatFitsInteger<T>(value)) return fallback;
        return static_cast<T>(value);
    }
    else {
        return fallback;
    }
}
} // namespace Arcbit
