// Based on https://www.preney.ca/paul/archives/1099, with Xeo's make_index_sequence
#ifndef UTIL_APPLYTUPLE_HPP
#define UTIL_APPLYTUPLE_HPP

#include <util/index_sequence.hpp>

#include <tuple>
#include <utility>

namespace util {

namespace detail {

template <class F, class Tuple, template <size_t...> class I, size_t... Indices>
inline auto applyTupleImpl (F&& f, Tuple&& t, I<Indices...>&&)
    -> decltype(std::forward<F>(f)(std::get<Indices>(std::forward<Tuple>(t))...))
{
    return std::forward<F>(f)(std::get<Indices>(std::forward<Tuple>(t))...);
}

} // namespace detail

// Given a function object f and a tuple of objects t = {...}:
//   applyTuple(f, t);
// is equivalent to
//   f(...);
// It should be equivalent to std::experimental::apply.
// Due to a compiler bug in VS2013, we have to use a void return type here. Go
// back to trailing return type when we can switch to VS2015.
template <class F, class Tuple,
    class Indices = typename make_index_sequence<std::tuple_size<typename std::decay<Tuple>::type>::value>::type
>
inline auto applyTuple (F&& f, Tuple&& t)
    -> decltype(detail::applyTupleImpl(std::forward<F>(f), std::forward<Tuple>(t), Indices{}))
{
    return detail::applyTupleImpl(std::forward<F>(f), std::forward<Tuple>(t), Indices{});
}

} // namespace util

#endif
