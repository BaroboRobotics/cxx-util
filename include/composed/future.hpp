// Copyright (c) 2017 Barobo, Inc.
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

// An Asio-compatible future.
//
// Why, you ask? Isn't this redundant with {std,boost}::future? In real-world use cases, you need to
// wait on futures, and you typically don't want to wait forever. So, it's a timed operation. So,
// you need a timer. The std::future<T> solution doesn't suffice here because it is only able to
// block the current thread with a timeout -- but if we're using Asio, it's inappropriate to block
// the event loop thread. So we need to use an Asio timer.
//
// We could jury-rig a simple solution with a std::future<T> and an Asio timer, calling .then() on
// the future and having some quizzical diamond logic with the timer to make everything complete.
// This would work, but would be harder to debug, and would pessimize the common case: one would end
// up type-erasing not one but two different handlers, one for the std::future<T>::then() call, and
// one for the timer.async_wait() call.
//
// So, here's a future, presented as an Asio IO object. It's basically an Asio timer and a
// boost::optional<T> squashed together.

// TODO:
// - Unit test :(

#ifndef COMPOSED_FUTURE_HPP
#define COMPOSED_FUTURE_HPP

#include <composed/associated_logger.hpp>

#include <beast/core/async_result.hpp>

namespace composed {

// =======================================================================================
// future

template <class T>
class future {
public:
    explicit future(boost::asio::io_service& context): timer(context) {}

    template <class Token>
    auto async_wait(Token&& token) {
        return async_wait_until(
                boost::asio::steady_timer::clock_type::time_point::max(),
                std::forward<Token>(token));
    }
    // Wait for the value to be emplaced with no timeout. Calling async_wait more than once before
    // emplacement results in undefined behavior.

    template <class Duration, class Token>
    auto async_wait_for(Duration&& duration, Token&& token);
    // Same as async_wait, but wait for a specific duration.

    template <class TimePoint, class Token>
    auto async_wait_until(TimePoint&& tp, Token&& token);
    // Same as async_wait, but wait until a specific time point.

    void close() {
        timer.expires_at(boost::asio::steady_timer::clock_type::time_point::min());
    }
    void close(boost::system::error_code& ec) {
        timer.expires_at(boost::asio::steady_timer::clock_type::time_point::min(), ec);
    }
    // Cancel any outstanding and future wait operations.

    template <class... Args>
    void emplace(Args&&... args);
    // Set the value of this future and complete any oustanding wait operation.

    bool has_value() const { return !!value_; }
    const T& value() const { return *value_; }
    // Value accessors.

private:
    template <class Handler>
    struct wait_handler;

    template <class Handler>
    auto make_wait_handler(Handler&& h);

    boost::asio::steady_timer timer;
    boost::optional<T> value_;
};

template <class T>
template <class Handler>
struct future<T>::wait_handler {
    future& parent;
    Handler h;

    void operator()(const boost::system::error_code& ec) {
        if (parent.timer.expires_at() == boost::asio::steady_timer::clock_type::time_point::min()) {
            // We are closed.
            h(boost::asio::error::operation_aborted);
        }
        else if (ec) {
            // A plain old cancelled timer means we didn't timeout.
            h(boost::system::error_code{});
        }
        else {
            // And success means we timed out.
            h(boost::asio::error::timed_out);
        }
    }

    using logger_type = associated_logger_t<Handler>;
    logger_type get_logger() const { return get_associated_logger(h); }

    friend void* asio_handler_allocate(size_t size, wait_handler* self) {
        using boost::asio::asio_handler_allocate;
        return asio_handler_allocate(size, &self->h);
    }

    friend void asio_handler_deallocate(void* pointer, size_t size, wait_handler* self) {
        using boost::asio::asio_handler_deallocate;
        asio_handler_deallocate(pointer, size, &self->h);
    }

    template <class Function>
    friend void asio_handler_invoke(Function&& f, wait_handler* self) {
        using boost::asio::asio_handler_invoke;
        asio_handler_invoke(std::forward<Function>(f), &self->h);
    }

    friend bool asio_handler_is_continuation(wait_handler* self) {
        using boost::asio::asio_handler_is_continuation;
        return asio_handler_is_continuation(&self->h);
    }
};

template <class T>
template <class Handler>
auto future<T>::make_wait_handler(Handler&& h) {
    return wait_handler<std::decay_t<Handler>>{*this, std::forward<Handler>(h)};
}

template <class T>
template <class Duration, class Token>
auto future<T>::async_wait_for(Duration&& duration, Token&& token) {
    beast::async_completion<Token, void(boost::system::error_code)> init{token};

    if (timer.expires_at() != boost::asio::steady_timer::clock_type::time_point::min()) {
        timer.expires_from_now(std::forward<Duration>(duration));
    }
    timer.async_wait(make_wait_handler(std::move(init.completion_handler)));

    return init.result.get();
}

template <class T>
template <class TimePoint, class Token>
auto future<T>::async_wait_until(TimePoint&& tp, Token&& token) {
    beast::async_completion<Token, void(boost::system::error_code)> init{token};

    if (timer.expires_at() != boost::asio::steady_timer::clock_type::time_point::min()) {
        timer.expires_at(std::forward<TimePoint>(tp));
    }
    timer.async_wait(make_wait_handler(std::move(init.completion_handler)));

    return init.result.get();
}

template <class T>
template <class... Args>
void future<T>::emplace(Args&&... args) {
    value_.emplace(std::forward<Args>(args)...);
    boost::system::error_code ec;
    timer.cancel(ec);
}

}  // composed

#endif