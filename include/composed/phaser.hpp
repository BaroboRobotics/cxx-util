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

#include <boost/asio/steady_timer.hpp>

namespace composed {

template <class TrunkHandler>
struct phaser;

template <class TrunkHandler, class ResultHandler, class BranchHandler>
struct phaser_handler {
    phaser<TrunkHandler>& parent;
    ResultHandler result_handler;
    BranchHandler handler;

    template <class... Args>
    void operator()(Args&&... args) {
        beast_asio_helpers::invoke(
            [ &parent = parent
            , result_handler = result_handler
            , args = std::tuple<Args...>(std::forward<Args>(args)...)] {
                apply(result_handler, std::move(args));
                parent.decrement();
            }, parent.handler);
    }

    friend void* asio_handler_allocate(size_t size, phaser_handler* self) {
        return beast_asio_helpers::allocate(size, self->handler);
    }

    friend void asio_handler_deallocate(void* pointer, size_t size, phaser_handler* self) {
        beast_asio_helpers::deallocate(pointer, size, self->handler);
    }

    template <class Function>
    friend void asio_handler_invoke(Function&& function, phaser_handler* self) {
        beast_asio_helpers::invoke(std::forward<Function>(function), self->handler);
    }

    friend bool asio_handler_is_continuation(phaser_handler* self) {
        return true;  // TODO: compare performance when this is false
    }
};

template <class TrunkHandler>
struct phaser {
public:
    phaser(boost::asio::io_service& context, TrunkHandler h);

    struct discard_results {
        template <class... Args>
        void operator()(Args&&...) const {}
    };

    auto completion() { return completion(discard_results{}); }
    template <class ResultHandler>
    auto completion(ResultHandler&& rh) { return completion(std::forward<ResultHandler>(rh), handler); }
    template <class ResultHandler, class BranchHandler>
    auto completion(ResultHandler&& rh, BranchHandler&& other);
    // Increment this phaser and return a handler which:
    //   - forwards all of its hooks (asio_handler_invoke, etc.) to `other`
    //   - forwards the operation's results to `rh` on TrunkHandler's execution context
    //   - decrements this phaser on TrunkHandler's execution context
    //
    // IMPORTANT: The returned handler contains a reference to this phaser. Therefore, it is NOT
    // safe to move the phaser while there are any outstanding handlers.
    //
    // Note that `other` is never actually invoked. It is merely used to convey context.

    template <class Token>
    auto async_wait(Token&& token);
    // Wait until this phaser's value is zero (there are no outstanding handlers/operations). If
    // the phaser's value is already zero, this function merely acts like a `post()`.

private:
    template <class T, class U, class V>
    friend class phaser_handler;

    void decrement();
    void increment();

    boost::asio::steady_timer timer;
    TrunkHandler handler;  // just used for asio_handler_invoke
    size_t n = 0;
};

template <class TrunkHandler>
phaser<TrunkHandler>::phaser(boost::asio::io_service& context, TrunkHandler h)
    : timer(context, decltype(timer)::clock_type::time_point::min())
    , handler(std::move(h))
{}

template <class TrunkHandler>
template <class ResultHandler, class BranchHandler>
auto phaser<TrunkHandler>::completion(ResultHandler&& rh, BranchHandler&& other) {
    increment();
    using ResultHandlerD = std::decay_t<ResultHandler>;
    using BranchHandlerD = std::decay_t<BranchHandler>;
    return phaser_handler<TrunkHandler, ResultHandlerD, BranchHandlerD>{
        *this, std::forward<ResultHandler>(rh), std::forward<BranchHandler>(other)
    };
}

template <class TrunkHandler>
template <class Token>
auto phaser<TrunkHandler>::async_wait(Token&& token) {
    return timer.async_wait(std::forward<Token>(token));
}


template <class TrunkHandler>
void phaser<TrunkHandler>::decrement() {
    BOOST_ASSERT(n != 0);
    if (--n == 0) {
        timer.expires_at(decltype(timer)::clock_type::time_point::min());
    }
}

template <class TrunkHandler>
void phaser<TrunkHandler>::increment() {
    if (++n == 1) {
        timer.expires_at(decltype(timer)::clock_type::time_point::max());
    }
}

}  // composed

#endif
