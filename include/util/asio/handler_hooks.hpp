// Copyright (c) 2014-2016 Barobo, Inc.
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#ifndef UTIL_ASIO_HANDLER_HOOKS_HPP
#define UTIL_ASIO_HANDLER_HOOKS_HPP

#include <boost/asio/handler_alloc_hook.hpp>
#include <boost/asio/handler_invoke_hook.hpp>
#include <boost/asio/handler_continuation_hook.hpp>

#include <memory>

namespace util { namespace asio { namespace handler_hooks {

// The asio_handler_* functions need to be called from a namespace in which
// they are not defined. The handler_hooks namespace exists for that
// purpose.

template <class Context>
inline void* allocate (size_t size, Context& context) {
    using boost::asio::asio_handler_allocate;
    return asio_handler_allocate(size, std::addressof(context));
}

template <class Context>
inline void deallocate (void* pointer, size_t size, Context& context) {
    using boost::asio::asio_handler_deallocate;
    asio_handler_deallocate(pointer, size, std::addressof(context));
}

template <class Function, class Context>
inline void invoke (Function&& f, Context& context) {
    using boost::asio::asio_handler_invoke;
    asio_handler_invoke(std::forward<Function>(f), std::addressof(context));
}

template <class Context>
inline bool is_continuation (Context& context) {
    using boost::asio::asio_handler_is_continuation;
    return asio_handler_is_continuation(std::addressof(context));
}

}}} // namespace util::asio::handler_hooks

#endif
