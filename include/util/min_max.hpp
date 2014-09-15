#ifndef UTIL_MIN_MAX_HPP
#define UTIL_MIN_MAX_HPP

#include <utility>

namespace util {

template <class T>
constexpr T max (T&& x) {
    return std::forward<T>(x);
}

template <class T, class U, class... Us>
constexpr const typename std::common_type<T, U, Us...>::type
max (T&& x, U&& y, Us&&... ys) {
    return y > x
           ? max(y, std::forward<Us>(ys)...)
           : max(x, std::forward<Us>(ys)...);
}

} // namespace util

#endif
