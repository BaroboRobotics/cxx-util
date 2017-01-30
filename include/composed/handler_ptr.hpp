// Copyright (c) 2017 Barobo, Inc.
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// The interface, but not the implementation, of the class presented here is based on
// `beast::handler_ptr` by Vinnie Falco:
//
//     Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
//     Distributed under the Boost Software License, Version 1.0. (See accompanying
//     file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef COMPOSED_HANDLER_PTR_HPP
#define COMPOSED_HANDLER_PTR_HPP

#include <composed/handler_hooks.hpp>

#include <beast/core/handler_alloc.hpp>

#include <memory>
#include <scoped_allocator>
#include <type_traits>
#include <utility>

namespace composed {

template <class T, class Handler>
using scoped_allocator = std::scoped_allocator_adaptor<beast::handler_alloc<T, Handler>>;

template <class T, class Handler>
class handler_ptr {
public:
    template <class... Args>
    explicit handler_ptr(Handler&& handler, Args&&... args);
    // Construct an object of type T using `args`. The object is constructed in memory allocated by
    // `handler`'s Asio allocation hooks.
    //
    // If T satisfies the "uses-allocator" construction requirements, it will be passed an instance
    // of a std::scoped_allocator_adaptor<beast::handler_alloc<U, Handler>>, for some type U.

    Handler& handler() const { return dp->handler; }

    template <class... Args>
    void invoke(Args&&... args) {
        // Deallocate data and invoke the completion handler.
        dp.get_deleter()(dp.release())(std::forward<Args>(args)...);
    }

    T* get() const { return reinterpret_cast<T*>(&dp->storage); }
    T& operator*() const { return *get(); }
    T* operator->() const { return get(); }

private:
    struct data;
    struct half_deleter;
    struct deleter;
    using data_ptr = std::unique_ptr<data, deleter>;
    data_ptr dp;
};

// =======================================================================================
// Inline implementation

template <class T, class Handler>
struct handler_ptr<T, Handler>::data {
    data(Handler&& h) noexcept(std::is_nothrow_move_constructible<Handler>::value)
        : handler(std::move(h)) {}
    Handler handler;
    std::aligned_storage_t<sizeof(T), alignof(T)> storage;
};

template <class T, class Handler>
struct handler_ptr<T, Handler>::half_deleter {
    using data = typename handler_ptr<T, Handler>::data;
    Handler operator()(data* p) const noexcept {
        static_assert(std::is_nothrow_move_constructible<Handler>::value,
                "Handler move constructor must be noexcept");
        static_assert(std::is_nothrow_destructible<data>::value,
                "data destructor must be noexcept");

        auto handler = std::move(p->handler);
        p->~data();
        handler_hooks::deallocate(p, sizeof(data), handler);
        return handler;
    }

    operator deleter() const { return deleter{}; }
};

template <class T, class Handler>
struct handler_ptr<T, Handler>::deleter {
    Handler operator()(data* p) const noexcept {
        static_assert(std::is_nothrow_destructible<T>::value, "T destructor must be noexcept");

        reinterpret_cast<T*>(&p->storage)->~T();
        return half_deleter{}(p);
    }
};

template <class T, class Handler>
template <class... Args>
handler_ptr<T, Handler>::handler_ptr(Handler&& handler, Args&&... args) {
    auto vp = handler_hooks::allocate(sizeof(data), handler);

    static_assert(std::is_nothrow_move_constructible<Handler>::value,
            "Handler move constructor must be noexcept");
    static_assert(std::is_nothrow_constructible<data, Handler&&>::value,
            "data constructor must be noexcept");

    using half_data_ptr = std::unique_ptr<data, half_deleter>;
    auto p = half_data_ptr{new (vp) data{std::move(handler)}};

    using allocator_type = scoped_allocator<char, Handler>;
    auto alloc = allocator_type{p->handler};
    std::allocator_traits<allocator_type>::construct(
            alloc, reinterpret_cast<T*>(&p->storage), std::forward<Args>(args)...);

    dp = std::move(p);
}

}  // composed

#endif