// Copyright (c) 2014-2016 Barobo, Inc.
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

// Active Object infrastructure for Asio

#ifndef COMPOSED_OBJECT_HPP
#define COMPOSED_OBJECT_HPP

#include <composed/associated_logger.hpp>
#include <composed/stdlib.hpp>

#include <beast/core/handler_ptr.hpp>
#include <boost/asio/steady_timer.hpp>

#include <tuple>
#include <utility>

namespace composed {

template <class TrunkObject, class BranchObject>
struct joint {
    boost::asio::steady_timer timer;

    TrunkObject& trunk_object;
    BranchObject& branch_object;

    boost::system::error_code ec{};

    template <class Handler>
    joint(Handler&, TrunkObject& t, BranchObject& b)
        : timer(t.get_io_service())
        , trunk_object(t)
        , branch_object(b)
    {}
};

template <class Handler, class... Ts>
struct interruption_handler {
    // Bind args to a handler. Throw away all other args this handler receives at invocation-time.

    Handler handler;
    std::tuple<Ts...> bound_args;

    template <class DeducedHandler, class... DeducedTs>
    interruption_handler(DeducedHandler&& h, DeducedTs&&... ts)
        : handler(std::forward<DeducedHandler>(h))
        , bound_args(std::forward<DeducedTs>(ts)...)
    {}

    template <class... Args>
    void operator()(Args&&...) {
        apply(handler, std::move(bound_args));
    }

    // ===================================================================================
    // Handler hooks

    friend void* asio_handler_allocate(size_t size, interruption_handler* self) {
        return beast_asio_helpers::allocate(size, self->handler);
    }

    friend void asio_handler_deallocate(void* pointer, size_t size, interruption_handler* self) {
        beast_asio_helpers::deallocate(pointer, size, self->handler);
    }

    template <class Function>
    friend void asio_handler_invoke(Function&& f, interruption_handler* self) {
        beast_asio_helpers::invoke(std::forward<Function>(f), self->handler);
    }

    friend bool asio_handler_is_continuation(interruption_handler* self) {
        return beast_asio_helpers::is_continuation(self->handler);
    }

    using logger_type = associated_logger_t<Handler>;
    logger_type get_logger() const { return get_associated_logger(handler); }
};

template <class Handler, class... Args>
auto bind_interruption_handler(Handler&& h, Args&&... args) {
    return interruption_handler<Handler, std::decay_t<Args>...>{
        std::forward<Handler>(h), std::forward<Args>(args)...
    };
}

struct timeout_tag {};

template <class TrunkObject, class Handler>
struct timeout_joiner {
    beast::handler_ptr<joint<TrunkObject, boost::asio::steady_timer>, Handler> d;

    template <class DeducedHandler>
    timeout_joiner(TrunkObject& t, boost::asio::steady_timer& b, DeducedHandler&& h)
        : d(std::forward<DeducedHandler>(h), t, b)
    {}

    void operator()(timeout_tag, const boost::system::error_code& ec) {
        if (!ec && d) {
            auto lg = get_associated_logger(d.handler());
            BOOST_LOG(lg) << "Timed out";
            d->ec = boost::asio::error::timed_out;
            d->trunk_object.cancel();
        }
    }

    template <class... Args>
    void operator()(boost::system::error_code ec, Args&&... args) {
        auto& timer = d->branch_object;
        timer.expires_at(boost::asio::steady_timer::time_point::min());
        if (d->ec && ec == boost::asio::error::operation_aborted) {
            // d->ec overrides ec iff we were cancelled
            ec = d->ec;
        }
        auto h = bind_interruption_handler(d.release_handler(), ec, std::forward<Args>(args)...);
        timer.async_wait(std::move(h));
    }
};

template <class TrunkObject, class Handler>
auto make_timeout_joiner(TrunkObject& trunk_object, boost::asio::steady_timer& timer, Handler&& h) {
    return timeout_joiner<TrunkObject, Handler>{trunk_object, timer, std::forward<Handler>(h)};
}

template <class Joiner, class Tag>
struct branch_handler {
    Joiner joiner;
    Tag tag;

    template <class... Args>
    void operator()(Args&&... args) {
        joiner(tag, std::forward<Args>(args)...);
    }

    // ===================================================================================
    // Handler hooks

    friend void* asio_handler_allocate(size_t size, branch_handler* self) {
        return beast_asio_helpers::allocate(size, self->joiner.d.handler());
    }

    friend void asio_handler_deallocate(void* pointer, size_t size, branch_handler* self) {
        beast_asio_helpers::deallocate(pointer, size, self->joiner.d.handler());
    }

    template <class Function>
    friend void asio_handler_invoke(Function&& f, branch_handler* self) {
        beast_asio_helpers::invoke(std::forward<Function>(f), self->joiner.d.handler());
    }

    friend bool asio_handler_is_continuation(branch_handler* self) {
        return beast_asio_helpers::is_continuation(self->joiner.d.handler());
    }

    using logger_type = associated_logger_t<typename decltype(joiner.d)::handler_type>;
    logger_type get_logger() const { return get_associated_logger(joiner.d.handler()); }
};

template <class Joiner, class Tag>
auto branch(Joiner&& j, Tag t = {}) {
    return branch_handler<Joiner, Tag>{j, t};
}

template <class Joiner>
struct branch_handler<Joiner, void> {
    Joiner joiner;

    template <class... Args>
    void operator()(Args&&... args) {
        joiner(std::forward<Args>(args)...);
    }

    // ===================================================================================
    // Handler hooks

    friend void* asio_handler_allocate(size_t size, branch_handler* self) {
        return beast_asio_helpers::allocate(size, self->joiner.d.handler());
    }

    friend void asio_handler_deallocate(void* pointer, size_t size, branch_handler* self) {
        beast_asio_helpers::deallocate(pointer, size, self->joiner.d.handler());
    }

    template <class Function>
    friend void asio_handler_invoke(Function&& f, branch_handler* self) {
        beast_asio_helpers::invoke(std::forward<Function>(f), self->joiner.d.handler());
    }

    friend bool asio_handler_is_continuation(branch_handler* self) {
        return beast_asio_helpers::is_continuation(self->joiner.d.handler());
    }

    using logger_type = associated_logger_t<typename decltype(joiner.d)::handler_type>;
    logger_type get_logger() const { return get_associated_logger(joiner.d.handler()); }
};

template <class Joiner>
auto branch(Joiner&& j) {
    return branch_handler<Joiner, void>{j};
}

}  // composed

#endif
