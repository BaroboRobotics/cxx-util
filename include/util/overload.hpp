// Copyright Louis Dionne 2013-2016
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE.md or copy at http://boost.org/LICENSE_1_0.txt)
//
// Stolen from Boost.Hana, Barobo-ified, and hacked to work with gcc 4.9 by Harris Hancock

#ifndef UTIL_OVERLOAD_HPP
#define UTIL_OVERLOAD_HPP

#include <type_traits>
#include <utility>

namespace util { namespace _ {

template <typename F, typename ...G>
struct overload_t
    : overload_t<F>::type
    , overload_t<G...>::type
{
    using type = overload_t;
    using overload_t<F>::type::operator();
    using overload_t<G...>::type::operator();

    template <typename F_, typename ...G_>
    constexpr explicit overload_t(F_&& f, G_&& ...g)
        : overload_t<F>::type(static_cast<F_&&>(f))
        , overload_t<G...>::type(static_cast<G_&&>(g)...)
    { }
};

template <typename F>
struct overload_t<F> { using type = F; };

template <typename R, typename ...Args>
struct overload_t<R(*)(Args...)> {
    using type = overload_t;
    R (*fptr_)(Args...);

    explicit constexpr overload_t(R (*fp)(Args...))
        : fptr_(fp)
    { }

    constexpr R operator()(Args ...args) const
    { return fptr_(static_cast<Args&&>(args)...); }
};

struct make_overload_t {
    template <typename ...F,
        typename Overload = typename overload_t<
            typename std::decay<F>::type...
        >::type
    >
    constexpr Overload operator()(F&& ...f) const {
        return Overload(static_cast<F&&>(f)...);
    }
};

}  // _

constexpr _::make_overload_t overload{};

namespace _ {

template <typename F, typename G>
struct overload_linearly_t {
    F f;
    G g;

private:
    template <typename ...Args, typename =
        decltype(std::declval<F const&>()(std::declval<Args>()...))>
    constexpr F const& which(int) const& { return f; }

#if 0
    // gcc 4.9 seems to have trouble with ref-qualified overloads

    template <typename ...Args, typename =
        decltype(std::declval<F&>()(std::declval<Args>()...))>
    constexpr F& which(int) & { return f; }

    template <typename ...Args, typename =
        decltype(std::declval<F&&>()(std::declval<Args>()...))>
    constexpr F which(int) && { return static_cast<F&&>(f); }
#endif

    template <typename ...Args>
    constexpr G const& which(long) const& { return g; }

#if 0
    template <typename ...Args>
    constexpr G& which(long) & { return g; }

    template <typename ...Args>
    constexpr G which(long) && { return static_cast<G&&>(g); }
#endif

public:
    template <typename ...Args>
    constexpr decltype(auto) operator()(Args&& ...args) const&
    { return which<Args...>(int{})(static_cast<Args&&>(args)...); }

#if 0
// gcc 4.9 doesn't like this
    template <typename ...Args>
    constexpr decltype(auto) operator()(Args&& ...args) &
    { return which<Args...>(int{})(static_cast<Args&&>(args)...); }

    template <typename ...Args>
    constexpr decltype(auto) operator()(Args&& ...args) &&
    { return which<Args...>(int{})(static_cast<Args&&>(args)...); }
#endif
};

struct make_overload_linearly_t {
    template <typename F, typename G>
    constexpr overload_linearly_t<
        typename std::decay<F>::type,
        typename std::decay<G>::type
    > operator()(F&& f, G&& g) const {
        return {static_cast<F&&>(f), static_cast<G&&>(g)};
    }

    template <typename F, typename G, typename ...H>
    constexpr decltype(auto) operator()(F&& f, G&& g, H&& ...h) const {
        return (*this)(static_cast<F&&>(f),
                (*this)(static_cast<G&&>(g), static_cast<H&&>(h)...));
    }

    template <typename F>
    constexpr typename std::decay<F>::type operator()(F&& f) const {
        return static_cast<F&&>(f);
    }
};

}  // _

constexpr _::make_overload_linearly_t overloadLinearly{};

}  // util

#endif