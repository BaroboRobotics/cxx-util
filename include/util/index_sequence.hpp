// Copyright (c) 2014-2016 Barobo, Inc.
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#ifndef UTIL_INDEX_SEQUENCE_HPP
#define UTIL_INDEX_SEQUENCE_HPP

#include <cstddef>

namespace util {

// based on http://stackoverflow.com/a/17426611/410767 by Xeo
template <size_t... Ints>
struct index_sequence
{
    using type = index_sequence;
    using value_type = std::size_t;
    static const std::size_t size() { return sizeof...(Ints); }
};

template <class Sequence1, class Sequence2>
struct merge_and_renumber;

template <size_t... I1, size_t... I2>
struct merge_and_renumber<index_sequence<I1...>, index_sequence<I2...>>
  : index_sequence<I1..., (sizeof...(I1)+I2)...>
{ };

template <size_t N>
struct make_index_sequence
  : merge_and_renumber<typename make_index_sequence<N/2>::type,
                        typename make_index_sequence<N - N/2>::type>
{ };

template<> struct make_index_sequence<0> : index_sequence<> { };
template<> struct make_index_sequence<1> : index_sequence<0> { };

template <size_t N>
using make_index_sequence_t = typename make_index_sequence<N>::type;
} // namespace util

#endif
