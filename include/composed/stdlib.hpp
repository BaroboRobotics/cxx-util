// Copyright (c) 2014-2016 Barobo, Inc.
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

// A collection of useful utilities from C++14 and C++17, backported to C++11.

#ifndef COMPOSED_INDEX_SEQUENCE_HPP
#define COMPOSED_INDEX_SEQUENCE_HPP

#include <tuple>
#include <type_traits>
#include <utility>
#include <cstddef>

namespace composed {

// =======================================================================================
// std::void_t (C++17)

template <class...> struct void_ { using type = void; };
template <class... Ts> using void_t = typename void_<Ts...>::type;
// Can't be a simple `template <class...> using void_t = void`, because of a
// language defect not fixed until C++14.

// =======================================================================================
// std::integer_sequence, std::index_sequence (C++14)

// based on http://stackoverflow.com/a/17426611/410767 by Xeo
template <class T, T... Ints>
struct integer_sequence {
    using type = integer_sequence;
    using value_type = T;
    static constexpr T size() { return sizeof...(Ints); }
};

namespace _ {

template <class Sequence1, class Sequence2>
struct merge_and_renumber;

template <class T, T... I1, T... I2>
struct merge_and_renumber<integer_sequence<T, I1...>, integer_sequence<T, I2...>>
    : integer_sequence<T, I1..., (sizeof...(I1) + I2)...> {
};

}  // _

template <class T, T N, class = void>
struct make_integer_sequence_impl
    : _::merge_and_renumber<typename make_integer_sequence_impl<T, N/2>::type,
                            typename make_integer_sequence_impl<T, N - N/2>::type> {};

template <class T, T I>
struct make_integer_sequence_impl<T, I, std::enable_if_t<I == 0>>: integer_sequence<T> {};
template <class T, T I>
struct make_integer_sequence_impl<T, I, std::enable_if_t<I == 1>>: integer_sequence<T, 0> {};

// Helper templates

template <size_t... Ints>
using index_sequence = integer_sequence<size_t, Ints...>;

template <class T, T N>
using make_integer_sequence = typename make_integer_sequence_impl<T, N>::type;
template <size_t N>
using make_index_sequence = make_integer_sequence<size_t, N>;

template <class... Ts>
using index_sequence_for = make_index_sequence<sizeof...(Ts)>;

// =======================================================================================
// std::apply (C++17)
// Based on https://www.preney.ca/paul/archives/1099

namespace _ {

template <class F, class Tuple, size_t... Indices>
constexpr decltype(auto) apply_impl(F&& f, Tuple&& t, composed::index_sequence<Indices...>) {
    return std::forward<F>(f)(std::get<Indices>(std::forward<Tuple>(t))...);
}

}  // _

// Given a function object f and a tuple of objects t = {...}:
//   apply(f, t);
// is equivalent to
//   f(...);
template <class F, class Tuple>
constexpr decltype(auto) apply(F&& f, Tuple&& t) {
    return _::apply_impl(
            std::forward<F>(f), std::forward<Tuple>(t),
            composed::make_index_sequence<std::tuple_size<std::decay_t<Tuple>>::value>{});
}

// =======================================================================================
// decay_copy (N3255)

template <class T> std::decay_t<T> decay_copy(T&& v) { return std::forward<T>(v); }

}  // composed

#endif
