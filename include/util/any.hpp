#ifndef UTIL_ANY_HPP
#define UTIL_ANY_HPP

#include <utility>

namespace util {

template <class T>
constexpr bool any (T&& x) {
    return std::forward<T>(x);
}

template <class T, class... Ts>
constexpr bool any (T&& x, Ts&&... xs) {
    return std::forward<T>(x) || any(std::forward<Ts>(xs)...);
}

} // namespace util

#endif
