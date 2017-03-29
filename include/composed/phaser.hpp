// Copyright (c) 2017 Barobo, Inc.
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

// An Asio-compatible phaser implementation.

// TODO:
// - Unit test :(
// - Factor out the handler-context-adoption feature of phaser_handler into a separate class.

#ifndef COMPOSED_PHASER_HPP
#define COMPOSED_PHASER_HPP

#include <composed/stdlib.hpp>

#include <beast/core/handler_helpers.hpp>
#include <beast/core/handler_ptr.hpp>

#include <boost/asio/steady_timer.hpp>

namespace composed {

template <class Context>
struct phaser {
    // An executor with an `async_wait` operation to wait for all work objects to be destroyed. You
    // can use `phaser.wrap(h)` to attach a work object to your handler and adopt this phaser's
    // context for `asio_handler_*` hooks.

public:
    phaser(boost::asio::io_service& context, Context h);

    struct work;
    // Increment this phaser's work count on construction, decrement on destruction.

    template <class Handler>
    auto wrap(Handler&& h);
    // Attach a work object to `h` and forward all asio_handler_* hooks to this phaser's context.
    //
    // IMPORTANT: The returned handler contains a reference to this phaser. Therefore, it is NOT
    // safe to move the phaser while there are any outstanding handlers.

#if 0
    template <class CompletionHandler>
    void dispatch(CompletionHandler&& h);

    template <class CompletionHandler>
    void post(CompletionHandler&& h);
#endif

    template <class Token>
    auto async_wait(Token&& token);
    // Wait until this phaser's work count is zero (there are no outstanding handlers/operations).
    // If the phaser's work count is already zero, this function merely acts like a `post()`.

    const Context& context() const { return handler; }

private:
    void remove_work();
    void add_work();

    boost::asio::steady_timer timer;
    Context handler;  // just used for asio_handler_invoke
    size_t work_count = 0;
};

// =============================================================================
// Inline implementation

template <class Context>
struct phaser<Context>::work {
    explicit work(phaser& p): parent(p) {
        parent.add_work();
    }

    work(const work& other): parent(other.parent) {
        parent.add_work();
    }

    work(work&& other) = delete;

    ~work() {
        parent.remove_work();
    }

    phaser<Context>& parent;
};

template <class Context, class Handler>
struct phaser_handler {
    struct data {
        data(Handler&, const typename phaser<Context>::work& w): work(w) {}
        typename phaser<Context>::work work;
    };

    beast::handler_ptr<data, Handler> d;
    // We need to use a handler_ptr to ensure ~work is called during a handler callback rather than
    // during just a regular destructor call.

    template <class DeducedHandler>
    phaser_handler(const typename phaser<Context>::work& w, DeducedHandler&& h)
        : d(std::forward<DeducedHandler>(h), w)
    {}

    template <class... Args>
    void operator()(Args&&... args) {
        const auto& context = d->work.parent.context();
        beast_asio_helpers::invoke(
            [ d = std::move(d)
            , args = std::tuple<Args...>(std::forward<Args>(args)...)]() mutable {
                auto work = d->work;  // Keep the phase alive at least until the end of this block.
                apply([&d](auto&&... as) { d.invoke(std::forward<decltype(as)>(as)...); }, std::move(args));
            }, context);
    }

    friend void* asio_handler_allocate(size_t size, phaser_handler* self) {
        return beast_asio_helpers::allocate(size, self->d->work.parent.context());
    }

    friend void asio_handler_deallocate(void* pointer, size_t size, phaser_handler* self) {
        beast_asio_helpers::deallocate(pointer, size, self->d->work.parent.context());
    }

    template <class Function>
    friend void asio_handler_invoke(Function&& function, phaser_handler* self) {
        beast_asio_helpers::invoke(std::forward<Function>(function), self->d->work.parent.context());
    }

    friend bool asio_handler_is_continuation(phaser_handler* self) {
        return true;  // TODO: compare performance when this is false
    }
};

template <class Context>
phaser<Context>::phaser(boost::asio::io_service& context, Context h)
    : timer(context, decltype(timer)::clock_type::time_point::min())
    , handler(std::move(h))
{}

template <class Context>
template <class Handler>
auto phaser<Context>::wrap(Handler&& rh) {
    return phaser_handler<Context, std::decay_t<Handler>>{work{*this}, std::forward<Handler>(rh)};
}

template <class Context>
template <class Token>
auto phaser<Context>::async_wait(Token&& token) {
    return timer.async_wait(std::forward<Token>(token));
}

template <class Context>
void phaser<Context>::remove_work() {
    BOOST_ASSERT(work_count != 0);
    if (--work_count == 0) {
        timer.expires_at(decltype(timer)::clock_type::time_point::min());
    }
}

template <class Context>
void phaser<Context>::add_work() {
    if (++work_count == 1) {
        timer.expires_at(decltype(timer)::clock_type::time_point::max());
    }
}

}  // composed

#endif
