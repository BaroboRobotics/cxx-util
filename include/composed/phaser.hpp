// Copyright (c) 2017 Barobo, Inc.
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

// An Asio-compatible phaser implementation.

// TODO:
// - Unit test :(

#ifndef COMPOSED_PHASER_HPP
#define COMPOSED_PHASER_HPP

#include <composed/stdlib.hpp>
#include <composed/work.hpp>

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

private:
    friend struct work<phaser<Context>>;

    const Context& context() const { return handler; }

    void remove_work();
    void add_work();

    boost::asio::steady_timer timer;
    Context handler;  // just used for asio_handler_invoke
    size_t work_count = 0;
};

// =============================================================================
// Inline implementation

template <class Context>
phaser<Context>::phaser(boost::asio::io_service& context, Context h)
    : timer(context, decltype(timer)::clock_type::time_point::min())
    , handler(std::move(h))
{}

template <class Context>
template <class Handler>
auto phaser<Context>::wrap(Handler&& rh) {
    return work_handler<phaser<Context>, std::decay_t<Handler>>{make_work(*this), std::forward<Handler>(rh)};
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
