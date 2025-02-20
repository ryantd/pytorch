#pragma once

#include <limits>
#include <type_traits>

namespace c10 {

/// Returns false since we cannot have x < 0 if x is unsigned.
template <typename T>
static inline constexpr bool is_negative(
    const T& x,
    std::true_type is_unsigned) {
  return false;
}

/// Returns true if a signed variable x < 0
template <typename T>
static inline constexpr bool is_negative(
    const T& x,
    std::false_type is_unsigned) {
  return x < T(0);
}

/// Returns true if x < 0
/// NOTE: Will fail on an unsigned custom type
///       For the most part it's possible to fix this if
///       the custom type has a constexpr constructor.
///       However, notably, c10::Half does not :-(
template <typename T>
inline constexpr bool is_negative(const T& x) {
  return is_negative(x, std::is_unsigned<T>());
}

/// Returns the sign of an unsigned variable x as 0, 1
template <typename T>
static inline constexpr int signum(const T& x, std::true_type is_unsigned) {
  return T(0) < x;
}

/// Returns the sign of a signed variable x as -1, 0, 1
template <typename T>
static inline constexpr int signum(const T& x, std::false_type is_unsigned) {
  return (T(0) < x) - (x < T(0));
}

/// Returns the sign of x as -1, 0, 1
/// NOTE: Will fail on an unsigned custom type
///       For the most part it's possible to fix this if
///       the custom type has a constexpr constructor.
///       However, notably, c10::Half does not :-(
template <typename T>
inline constexpr int signum(const T& x) {
  return signum(x, std::is_unsigned<T>());
}

/// Returns true if a and b are not both negative
template <typename T, typename U>
inline constexpr bool signs_differ(const T& a, const U& b) {
  return is_negative(a) != is_negative(b);
}

/// Returns true if x is greater than the greatest value of the type Limit
template <typename Limit, typename T>
inline constexpr bool greater_than_max(const T& x) {
  constexpr bool can_overflow =
      std::numeric_limits<T>::digits > std::numeric_limits<Limit>::digits;
  return can_overflow && x > std::numeric_limits<Limit>::max();
}

} // namespace c10
