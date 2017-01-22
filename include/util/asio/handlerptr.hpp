// Copyright (c) 2014-2016 Barobo, Inc.
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#ifndef UTIL_ASIO_HANDLERPTR_HPP
#define UTIL_ASIO_HANDLERPTR_HPP

#include <util/asio/handler_hooks.hpp>

#include <beast/core/handler_alloc.hpp>

#include <memory>
#include <scoped_allocator>
#include <type_traits>
#include <utility>

namespace util { namespace asio {

template <class T, class Handler>
class HandlerPtr {
public:
    template <class... Args>
    explicit HandlerPtr(Handler&& handler, Args&&... args);
    // Construct an object of type T using `args`. The object is constructed in memory allocated by
    // `handler`'s Asio allocation hooks.
    //
    // If T satisfies the "uses-allocator" construction requirements, it will be passed an instance
    // of a std::scoped_allocator_adaptor<beast::handler_alloc<U, Handler>>, for some type U.

    Handler& handler() const { return data->handler; }

    template <class... Args>
    void invoke(Args&&... args) {
        // Deallocate data and invoke the completion handler.
        data.get_deleter()(data.release())(std::forward<Args>(args)...);
    }

    T* get() const { return reinterpret_cast<T*>(&data->storage); }
    T& operator*() const { return *get(); }
    T* operator->() const { return get(); }

private:
    struct Data;
    struct HalfDeleter;
    struct Deleter;
    using DataPtr = std::unique_ptr<Data, Deleter>;
    DataPtr data;
};

// =======================================================================================
// Inline implementation

template <class T, class Handler>
struct HandlerPtr<T, Handler>::Data {
    Data(Handler&& h) noexcept(std::is_nothrow_move_constructible<Handler>::value)
        : handler(std::move(h)) {}
    Handler handler;
    std::aligned_storage_t<sizeof(T), alignof(T)> storage;
};

template <class T, class Handler>
struct HandlerPtr<T, Handler>::HalfDeleter {
    Handler operator()(Data* p) const noexcept {
        static_assert(std::is_nothrow_move_constructible<Handler>::value,
                "Handler move constructor must be noexcept");
        static_assert(std::is_nothrow_destructible<Data>::value,
                "Data destructor must be noexcept");

        auto handler = std::move(p->handler);
        p->~Data();
        handler_hooks::deallocate(p, sizeof(Data), handler);
        return handler;
    }

    operator Deleter() const { return Deleter{}; }
};

template <class T, class Handler>
struct HandlerPtr<T, Handler>::Deleter {
    Handler operator()(Data* p) const noexcept {
        static_assert(std::is_nothrow_destructible<T>::value, "T destructor must be noexcept");

        reinterpret_cast<T*>(&p->storage)->~T();
        return HalfDeleter{}(p);
    }
};

template <class T, class Handler>
template <class... Args>
HandlerPtr<T, Handler>::HandlerPtr(Handler&& handler, Args&&... args) {
    auto vp = handler_hooks::allocate(sizeof(Data), handler);

    static_assert(std::is_nothrow_move_constructible<Handler>::value,
            "Handler move constructor must be noexcept");
    static_assert(std::is_nothrow_constructible<Data, Handler&&>::value,
            "Data constructor must be noexcept");

    using HalfDataPtr = std::unique_ptr<Data, HalfDeleter>;
    auto p = HalfDataPtr{new (vp) Data{std::move(handler)}};

    using Alloc = std::scoped_allocator_adaptor<beast::handler_alloc<char, Handler>>;
    auto alloc = Alloc{p->handler};
    std::allocator_traits<Alloc>::construct(alloc, reinterpret_cast<T*>(&p->storage),
            std::forward<Args>(args)...);

    data = std::move(p);
}

}}

#endif