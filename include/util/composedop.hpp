#ifndef UTIL_COMPOSEDOPERATION_HPP
#define UTIL_COMPOSEDOPERATION_HPP

#include <util/asio_handler_helpers.hpp>

#include <boost/asio/coroutine.hpp>

#include <memory>
#include <utility>

namespace util {

template <class StateBase, class Handler>
struct ComposedOp : boost::asio::coroutine {
    struct State : StateBase {
        template <class S, class H>
        State (S&& s, H&& h)
            : StateBase(std::forward<S>(s))
            , handler(std::forward<H>(h))
        {}
        Handler handler;
    };

    std::shared_ptr<State> m;

    template <class S, class H>
    ComposedOp (S&& s, H&& h)
        : m(std::make_shared<State>(std::forward<S>(s), std::forward<H>(h)))
    {}

    template <class... Args>
    void operator() (Args&&... args) { (*m)(*this, std::forward<Args>(args)...); }

    template <class... Args>
    void complete (Args&&... args) { m->handler(std::forward<Args>(args)...); }

    // Inherit the allocation, invocation, and continuation strategies from the
    // operation's completion handler.
    friend void* asio_handler_allocate (size_t size, ComposedOp* self) {
        return asio_handler_helpers::allocate(size, self->m->handler);
    }

    friend void asio_handler_deallocate (void* pointer, size_t size, ComposedOp* self) {
        asio_handler_helpers::deallocate(pointer, size, self->m->handler);
    }

    template <class Function>
    friend void asio_handler_invoke (Function&& f, ComposedOp* self) {
        asio_handler_helpers::invoke(std::forward<Function>(f), self->m->handler);
    }

    friend bool asio_handler_is_continuation (ComposedOp* self) {
        return asio_handler_helpers::is_continuation(self->m->handler)
    }
};

} // namespace util

#endif