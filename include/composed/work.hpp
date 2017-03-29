// Copyright (c) 2017 Barobo, Inc.
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

// - Factor out the handler-context-adoption feature of work_handler into a separate class.

#ifndef COMPOSED_WORK_HPP
#define COMPOSED_WORK_HPP

#include <composed/stdlib.hpp>

#include <beast/core/handler_helpers.hpp>
#include <beast/core/handler_ptr.hpp>

#include <tuple>
#include <utility>

namespace composed {

template <class Executor>
struct work {
    explicit work(Executor& p): parent(p) {
        parent.add_work();
    }

    work(const work& other): parent(other.parent) {
        parent.add_work();
    }

    work(work&& other): parent(other.parent) {
        parent.add_work();  // we have no moved-from state (yet), so just act like a copy
    }

    ~work() {
        parent.remove_work();
    }

    decltype(auto) context() const { return parent.context(); }

    Executor& parent;
};

template <class Executor>
work<Executor> make_work(Executor& e) {
    return work<Executor>{e};
}

template <class Executor, class Handler>
struct work_handler {
    // A handler which adopts the work's executor's asio_handler_* hook context and carries a work
    // object for that executor.
    //
    // TODO: these are two separate things: work object attachment and context adoption. Factor them
    // out.

    struct data {
        data(Handler&, const work<Executor>& w): work_(w) {}
        work<Executor> work_;
    };

    beast::handler_ptr<data, Handler> d;
    // We need to use a handler_ptr to ensure ~work is called during a handler callback rather than
    // during just a regular destructor call.

    template <class DeducedHandler>
    work_handler(const work<Executor>& w, DeducedHandler&& h)
        : d(std::forward<DeducedHandler>(h), w)
    {}

    template <class... Args>
    void operator()(Args&&... args) {
        const auto& context = d->work_.context();
        beast_asio_helpers::invoke(
            [ d = std::move(d)
            , args = std::tuple<Args...>(std::forward<Args>(args)...)]() mutable {
                auto work = d->work_;  // Keep the work alive at least until the end of this block.
                apply([&d](auto&&... as) { d.invoke(std::forward<decltype(as)>(as)...); }, std::move(args));
            }, context);
    }

    friend void* asio_handler_allocate(size_t size, work_handler* self) {
        return beast_asio_helpers::allocate(size, self->d->work_.context());
    }

    friend void asio_handler_deallocate(void* pointer, size_t size, work_handler* self) {
        beast_asio_helpers::deallocate(pointer, size, self->d->work_.context());
    }

    template <class Function>
    friend void asio_handler_invoke(Function&& function, work_handler* self) {
        beast_asio_helpers::invoke(std::forward<Function>(function), self->d->work_.context());
    }

    friend bool asio_handler_is_continuation(work_handler* self) {
        return true;  // TODO: compare performance when this is false
    }
};

}  // composed

#endif