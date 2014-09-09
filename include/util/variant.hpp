#ifndef UTIL_VARIANT_HPP
#define UTIL_VARIANT_HPP

#include "min_max.hpp"

#include <new>
#include <type_traits>
#include <utility>

#include <cassert>

namespace util {

namespace detail {

template <class U>
inline void dtor (void* data) {
    reinterpret_cast<U*>(data)->~U();
}

template <>
inline void dtor<void> (void*) { }

using Dtor = void(*)(void*);

template <class F>
inline void apply (Dtor, F&&, void*) {
    assert(false && "visitor application to unbounded type");
}

template <class F, class T, class... Ts>
inline void apply (Dtor d, F&& f, void* data) {
    &dtor<T> == d ? std::forward<F>(f)(*reinterpret_cast<T*>(data))
                  : apply<F, Ts...>(d, std::forward<F>(f), data);
}

template <class T>
constexpr bool any (T&& x) {
    return std::forward<T>(x);
}

template <class T, class... Ts>
constexpr bool any (T&& x, Ts&&... xs) {
    return std::forward<T>(x) || any(std::forward<Ts>(xs)...);
}

} // namespace detail

template <class... Ts>
class Variant {
public:
    static_assert(!detail::any(std::is_reference<Ts>::value...),
            "bounded types may not be references");

    static_assert(!detail::any(std::is_const<Ts>::value...),
            "bounded types may not be const");

    friend void swap (Variant& lhs, Variant& rhs) noexcept {
        using std::swap;
        static_assert(noexcept(swap(lhs.mDtor, rhs.mDtor)) &&
                      noexcept(swap(lhs.mData, rhs.mData)), "throwing swap");
        swap(lhs.mDtor, rhs.mDtor);
        swap(lhs.mData, rhs.mData);
    }

    Variant () = delete;                // not DefaultConstructible
    Variant (const Variant&) = delete;  // not CopyConstructible

    Variant (Variant&& other) noexcept {
        swap(*this, other);
    }

    Variant& operator= (Variant other) noexcept {
        swap(*this, other);
        return *this;
    }

    template <class U>
    Variant (U&& x) noexcept(std::is_nothrow_move_constructible<U>::value)
            : mDtor(&detail::dtor<U>) {
        static_assert(detail::any(std::is_same<U, Ts>::value...),
                "variant construction from unbounded type");
        new (&mData) U(std::forward<U>(x));
    }

    ~Variant () {
        mDtor(&mData);
    }

    template <class F>
    void apply (F&& f) {
        detail::apply<F, Ts...>(mDtor, std::forward<F>(f), &mData);
    }

private:
    detail::Dtor mDtor = &detail::dtor<void>;
    typename std::aligned_storage< max(sizeof(Ts)...)
                                 , max(alignof(Ts)...)
                                 >::type mData;
};

} // namespace util

#endif
