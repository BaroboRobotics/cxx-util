#ifndef UTIL_VARIANT_HPP
#define UTIL_VARIANT_HPP

#include "util/any.hpp"
#include "util/min_max.hpp"

#include <boost/predef.h> // to check for gcc < 4.8

#include <new>
#include <tuple>
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

} // namespace detail

/* A Variant acts as a union of its parameterized types, so you can do things
 * like this:
 *
 *     Variant<int, const char*> v = "some string";
 *     v = 23;
 *
 * The Variant's parameterized types (int and const char* in the example) are
 * referred to as its "bounded types," a term borrowed from Boost. The Variant
 * template allocates enough appropriately aligned storage on the stack to hold
 * the largest and most strictly aligned of the bounded types. On a 64-bit
 * architecture, a Variant<char, long double> named v would have sizeof(v) ==
 * 16+8 and alignof(v) == 16.
 *
 * The extra 8 bytes in the sizeof(v) is taken up by a pointer to the current
 * value's destructor (actually, a free function wrapping the destructor).
 *
 * Variant values themselves (like v, in the example), are move-only, even if
 * their bounded types support copy construction/assignment. This restriction
 * simplifies the implementation. 
 *
 * The Variant type does not support the concept of nullability, and will not
 * automatically default-construct a value even if one of its bounded types is
 * default-constructible. Therefore, you must always provide a temporary value
 * of some bounded type to initialize it, even if that value itself is simply
 * default-constructed.
 *
 *     Variant<int, char> v = int();
 */

template <class... Ts>
class Variant {
public:
    static_assert(!any(std::is_reference<Ts>::value...),
            "bounded types may not be references");

    static_assert(!any(std::is_const<Ts>::value...),
            "bounded types may not be const");

// The standard library that comes with gcc < 4.8, or perhaps the compiler
// itself, is buggy: std::promise's move constructor is marked noexcept, but
// std::is_nothrow_move_constructible disagrees. This check might be more
// appropriate against the standard library version number, but ain't nobody
// got time for that.
#if !BOOST_COMP_GNUC || BOOST_COMP_GNUC >= BOOST_VERSION_NUMBER(4,8,0)
    static_assert(!any(!std::is_nothrow_move_constructible<Ts>::value...),
            "bounded types must be nothrow move-constructible");
#else
# warning Your version of gcc does not have a nothrow move-constructible std::promise.
# warning Disabling nothrow move-constructible checks in util::Variant.
# warning Upgrade to gcc 4.8+.
#endif

    template <class... Us>
    friend void swap (Variant<Us...>& lhs, Variant<Us...>& rhs) noexcept;

    Variant (const Variant&) = delete;  // not CopyConstructible

    Variant (Variant&& other) noexcept {
        swap(*this, other);
    }

    Variant& operator= (Variant other) noexcept {
        swap(*this, other);
        return *this;
    }

    template <class U>
    Variant (U&& x) noexcept : mDtor(&detail::dtor<U>) {
        static_assert(any(std::is_same<U, Ts>::value...),
                "variant construction from unbounded type");
        new (&mData) U(std::forward<U>(x));
    }

    ~Variant () {
        mDtor(&mData);
    }

    template <class U>
    U* get () {
        return &detail::dtor<U> == mDtor ? reinterpret_cast<U*>(&mData)
                                         : nullptr;
    }

private:
    // DefaultConstructible only from friends (i.e., swap).
    Variant () { }

    detail::Dtor mDtor = &detail::dtor<void>;
    typename std::aligned_storage< max(sizeof(Ts)...)
                                 , max(alignof(Ts)...)
                                 >::type mData;
};

namespace detail {

template <class F, class V>
inline void apply (F&&, V&) {
    assert(false && "visitor application to unbounded types");
}

template <class F, class V, class T, class... Ts>
inline void apply (F&& f, V& v) {
    auto ptr = v.template get<T>();
    ptr ? std::forward<F>(f)(*ptr)
        : apply<F, V, Ts...>(std::forward<F>(f), v);
}

// A visitor to help the Variant swap function out.
struct MoveTo {
    void* destination;

    MoveTo (void* data) : destination(data) { }

    template <class T>
    void operator() (T& x) {
        new (destination) T(std::move(x));
    }
};

} // namespace detail

template <class T, class... Ts>
inline T* get (Variant<Ts...>* v) {
    static_assert(any(std::is_same<T, Ts>::value...),
        "attempt to get unbounded type from variant");
    return v->template get<T>();
}

template <class F, class... Ts>
inline void apply (F&& f, Variant<Ts...>& v) {
    detail::apply<F, decltype(v), Ts...>(std::forward<F>(f), v);
}

template <class... Ts>
inline void swap (Variant<Ts...>& lhs, Variant<Ts...>& rhs) noexcept {
    // Can't swap values of potentially disparate types. Use a temporary.
    Variant<Ts...> tmp;
    tmp.mDtor = lhs.mDtor;
    apply(detail::MoveTo(&tmp.mData), lhs);
    lhs.mDtor = rhs.mDtor;
    apply(detail::MoveTo(&lhs.mData), rhs);
    rhs.mDtor = tmp.mDtor;
    apply(detail::MoveTo(&rhs.mData), tmp);
}

} // namespace util

#endif
