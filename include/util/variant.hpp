#ifndef UTIL_VARIANT_HPP
#define UTIL_VARIANT_HPP

#include "util/any.hpp"
#include "util/min_max.hpp"

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
 * Variant values themselves (like v, in the example), are move-only types,
 * even if their bounded types support copy construction/assignment. This
 * restriction greatly simplifies the implementation. 
 *
 * The Variant type does not support the concept of nullability, and will not
 * automatically default-construct a value even if one of its bounded types is
 * default-constructible. Therefore, you must always provide a temporary value
 * of some bounded type to initialize it, even if it is simply
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
    Variant (U&& x) noexcept(std::is_nothrow_move_constructible<U>::value)
            : mDtor(&detail::dtor<U>) {
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

// TODO figure out how to make these the same, variadic function
template <class F, class V>
inline void apply2 (F&&, V&, V&) {
    // This assertion is a programming error on our part.
    assert(false && "visitor application to unbounded types");
}

template <class F, class V, class T, class... Ts>
inline void apply2 (F&& f, V& v, V& w) {
    auto lhs = v.template get<T>();
    auto rhs = w.template get<T>();
    // This assertion is a programming error on the user's part.
    assert(!!lhs == !!rhs && "visitor application to variants holding "
                             "values of different types");
    lhs && rhs ? std::forward<F>(f)(*lhs, *rhs)
               : apply2<F, V, Ts...>(std::forward<F>(f), v, w);
}

// A couple visitors to help the Variant swap function out.
struct Swap {
    template <class T>
    void operator() (T& x, T& y) const {
        using std::swap;
        swap(x, y);
    }
};

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

template <class F, class... Ts>
inline void apply2 (F&& f, Variant<Ts...>& v, Variant<Ts...>& w) {
    detail::apply2<F, decltype(v), Ts...>(std::forward<F>(f), v, w);
}

template <class... Ts>
inline void swap (Variant<Ts...>& lhs, Variant<Ts...>& rhs) noexcept {
    using std::swap;

    // std::tuple's swap is noexcept if all of its type parameters' swaps are
    // noexcept. We can use this to assert that all of our bounded types must
    // be noexcept swappable. The destructor pointer is of course noexcept
    // swappable, but included in the check for completeness.
    static std::tuple<Ts...>* t = nullptr;
    static_assert(noexcept(t->swap(*t)), "bounded type has throwing swap");
    static_assert(noexcept(swap(lhs.mDtor, rhs.mDtor)), "throwing swap");

    if (lhs.mDtor == rhs.mDtor) {
        apply2(detail::Swap(), lhs, rhs);
    }
    else {
        // Can't swap two disparate types. Move one to a temporary instead.
        Variant<Ts...> tmp;
        tmp.mDtor = lhs.mDtor;
        apply(detail::MoveTo(&tmp.mData), lhs);
        lhs.mDtor = rhs.mDtor;
        apply(detail::MoveTo(&lhs.mData), rhs);
        rhs.mDtor = tmp.mDtor;
        apply(detail::MoveTo(&rhs.mData), tmp);
    }
}

} // namespace util

#endif
