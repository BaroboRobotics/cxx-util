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

#include <beast/core/handler_helpers.hpp>
#include <beast/core/handler_ptr.hpp>

#include <boost/asio/steady_timer.hpp>

namespace composed {

struct phaser {
    // An executor with an `async_wait` operation to wait for all work objects to be destroyed. You
    // can use `phaser.wrap(h)` to attach a work object to your handler and adopt this phaser's
    // context for `asio_handler_*` hooks.

public:
    phaser(boost::asio::io_service& context);

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

    // FIXME these should be const noexcept to satisfy Executor requirements in Net.TS
    void on_work_started();
    void on_work_finished();

private:
    boost::asio::steady_timer timer;
    size_t work_count = 0;
};

// =============================================================================
// Inline implementation

phaser::phaser(boost::asio::io_service& context)
    : timer(context, decltype(timer)::clock_type::time_point::min())
{}

template <class Token>
auto phaser::async_wait(Token&& token) {
    return timer.async_wait(std::forward<Token>(token));
}

void phaser::on_work_started() {
    if (++work_count == 1) {
        timer.expires_at(decltype(timer)::clock_type::time_point::max());
    }
}

void phaser::on_work_finished() {
    BOOST_ASSERT(work_count != 0);
    if (--work_count == 0) {
        timer.expires_at(decltype(timer)::clock_type::time_point::min());
    }
}

}  // composed

#endif
