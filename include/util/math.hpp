// Copyright (c) 2016 Barobo, Inc.
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#ifndef UTIL_MATH_HPP
#define UTIL_MATH_HPP

namespace util {

template <class T>
constexpr T pi() { return T(3.14159265358979323846); }

template <class T>
constexpr T degToRad (T x) { return T(double(x) * pi<double>() / 180.0); }

template <class T>
constexpr T radToDeg (T x) { return T(double(x) * 180.0 / pi<double>()); }

} // util

#endif