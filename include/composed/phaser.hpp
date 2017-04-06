// Copyright (c) 2017 Barobo, Inc.
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

// An Asio-compatible phaser implementation.

// TODO:
// - Unit test :(
// - make dispatch concurrently invokable

#ifndef COMPOSED_PHASER_HPP
#define COMPOSED_PHASER_HPP

#include <composed/associated_logger.hpp>
#include <composed/stdlib.hpp>
#include <composed/work_guard.hpp>

#include <beast/core/handler_helpers.hpp>
#include <beast/core/handler_ptr.hpp>

#include <boost/asio/steady_timer.hpp>

namespace composed {

struct phaser {
    // An executor whose `dispatch` function waits for all work objects to be destroyed before
    // executing the passed function object.

public:
    explicit phaser(boost::asio::io_service& context);

    template <class Handler>
    void dispatch(Handler&& h);
    // Wait until this phaser's work count is zero (there are no outstanding handlers/operations),
    // then dispatch `f`. If the phaser's work count is already zero, `f` is dispatched immediately.

    // FIXME these should be const to satisfy Executor requirements in Net.TS
    void on_work_started() noexcept;
    void on_work_finished() noexcept;

private:
    template <class Handler>
    struct ready_handler;

    template <class Handler>
    auto make_ready_handler(Handler&& h);

    template <class Handler>
    struct wait_handler;

    template <class Handler>
    auto make_wait_handler(Handler&& h);

    boost::asio::steady_timer timer;
    size_t work_count = 0;

    using time_point = decltype(timer)::clock_type::time_point;
};

// =============================================================================
// Inline implementation

phaser::phaser(boost::asio::io_service& context)
    : timer(context, time_point::min())
{}

template <class Handler>
struct phaser::ready_handler {
    work_guard<phaser> work;
    Handler h;

    void operator()() {
        beast_asio_helpers::invoke(h, h);
        // FIXME: make phaser wrap a strand and just invoke h().
    }

    using logger_type = associated_logger_t<Handler>;
    logger_type get_logger() const { return get_associated_logger(h); }

    friend void* asio_handler_allocate(size_t size, ready_handler* self) {
        return beast_asio_helpers::allocate(size, self->h);
    }

    friend void asio_handler_deallocate(void* pointer, size_t size, ready_handler* self) {
        beast_asio_helpers::deallocate(pointer, size, self->h);
    }

    template <class Function>
    friend void asio_handler_invoke(Function&& f, ready_handler* self) {
        beast_asio_helpers::invoke(std::forward<Function>(f), self->h);
    }

    friend bool asio_handler_is_continuation(ready_handler* self) {
        return beast_asio_helpers::is_continuation(self->h);
    }
};

template <class Handler>
auto phaser::make_ready_handler(Handler&& h) {
    return ready_handler<std::decay_t<Handler>>{make_work_guard(*this), std::forward<Handler>(h)};
}

template <class Handler>
struct phaser::wait_handler {
    phaser& parent;
    Handler h;

    void operator()(const boost::system::error_code&) { parent.make_ready_handler(std::move(h))(); }

    using logger_type = associated_logger_t<Handler>;
    logger_type get_logger() const { return get_associated_logger(h); }

    friend void* asio_handler_allocate(size_t size, wait_handler* self) {
        return beast_asio_helpers::allocate(size, self->h);
    }

    friend void asio_handler_deallocate(void* pointer, size_t size, wait_handler* self) {
        beast_asio_helpers::deallocate(pointer, size, self->h);
    }

    template <class Function>
    friend void asio_handler_invoke(Function&& f, wait_handler* self) {
        beast_asio_helpers::invoke(std::forward<Function>(f), self->h);
    }

    friend bool asio_handler_is_continuation(wait_handler* self) {
        return beast_asio_helpers::is_continuation(self->h);
    }
};

template <class Handler>
auto phaser::make_wait_handler(Handler&& h) {
    return wait_handler<std::decay_t<Handler>>{*this, std::forward<Handler>(h)};
}

template <class Handler>
void phaser::dispatch(Handler&& h) {
    if (timer.expires_at() == time_point::min()) {
        make_ready_handler(std::forward<Handler>(h))();
        // FIXME: make phaser wrap strand and strand.dispatch(h).
    }
    else {
        timer.async_wait(make_wait_handler(std::forward<Handler>(h)));
    }
}

void phaser::on_work_started() noexcept {
    if (++work_count == 1 && timer.expires_at() == time_point::min()) {
        boost::system::error_code ec;
        auto cancelled = timer.expires_at(time_point::max(), ec);
        BOOST_ASSERT(!ec && !cancelled);
    }
}

void phaser::on_work_finished() noexcept {
    BOOST_ASSERT(work_count != 0);
    boost::system::error_code ec;
    if (--work_count == 0 && timer.cancel_one(ec) == 0) {
        BOOST_ASSERT(!ec);
        auto cancelled = timer.expires_at(time_point::min(), ec);
        BOOST_ASSERT(!cancelled);
    }
    BOOST_ASSERT(!ec);
}

}  // composed

#endif
