// Based on https://www.preney.ca/paul/archives/1099, with Xeo's make_index_sequence
#ifndef UTIL_APPLYTUPLE_HPP
#define UTIL_APPLYTUPLE_HPP

#include <tuple>
#include <utility>

namespace util {

namespace detail {

// based on http://stackoverflow.com/a/17426611/410767 by Xeo
template <size_t... Ints>
struct index_sequence
{
    using type = index_sequence;
    using value_type = size_t;
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
    class Indices = detail::make_index_sequence<std::tuple_size<typename std::decay<Tuple>::type>::value>::type
>
inline auto applyTuple (F&& f, Tuple&& t)
    -> decltype(detail::applyTupleImpl(std::forward<F>(f), std::forward<Tuple>(t), Indices{}))
{
    return detail::applyTupleImpl(std::forward<F>(f), std::forward<Tuple>(t), Indices{});
}

} // namespace util

#endif