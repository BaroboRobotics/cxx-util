// Copyright (c) 2017 Barobo, Inc.
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

// Handler context adoption.
//
// You have some sort of final handler, `h`, and you have a function object `f`. You can guarantee
// that `h` outlives `f`, so `f` can hold a reference to `h` safely. You want `f` to adopt `h`'s
// allocation, execution, continuation optimization, and associated logger contexts.
//
// You call `bind_handler_context(h, std::move(f))`.

#ifndef COMPOSED_HANDLER_CONTEXT_HPP
#define COMPOSED_HANDLER_CONTEXT_HPP

#include <composed/associated_logger.hpp>

#include <beast/core/handler_helpers.hpp>

#include <type_traits>
#include <utility>

namespace composed {

template <class Handler, class HandlerCtx>
struct handler_context_binder {
    Handler handler;
    HandlerCtx& handler_context;

    template <class... Args>
    void operator()(Args&&... args) {
        handler(std::forward<Args>(args)...);
    }

    using logger_type = associated_logger_t<HandlerCtx>;
    logger_type get_logger() const { return get_associated_logger(handler_context); }

    friend void* asio_handler_allocate(size_t size, handler_context_binder* self) {
        return beast_asio_helpers::allocate(size, self->handler_context);
    }

    friend void asio_handler_deallocate(void* pointer, size_t size, handler_context_binder* self) {
        beast_asio_helpers::deallocate(pointer, size, self->handler_context);
    }

    template <class Function>
    friend void asio_handler_invoke(Function&& function, handler_context_binder* self) {
        beast_asio_helpers::invoke(std::forward<Function>(function), self->handler_context);
    }

    friend bool asio_handler_is_continuation(handler_context_binder* self) {
        return beast_asio_helpers::is_continuation(self->handler_context);
    }
};

template <class HandlerCtx, class Handler>
handler_context_binder<std::decay_t<Handler>, std::decay_t<HandlerCtx>>
bind_handler_context(HandlerCtx&& ctx, Handler&& h) {
    return {std::forward<Handler>(h), std::forward<HandlerCtx>(ctx)};
}

}  // composed

#endif
