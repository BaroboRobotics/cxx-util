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

#include <beast/core/handler_ptr.hpp>

#include <boost/asio/strand.hpp>
#include <boost/asio/steady_timer.hpp>

namespace composed {

template <class Executor>
struct phaser {
    // An executor whose `dispatch` function waits for all work objects to be destroyed before
    // executing the passed function object.

public:
    template <class... Args>
    explicit phaser(Args&&...);
    // Pass args through to the wrapped executor.

    boost::asio::io_service& get_io_service() { return executor.get_io_service(); }

    template <class Handler>
    void dispatch(Handler&& h);
    // Wait until this phaser's work count is zero (there are no outstanding handlers/operations),
    // then dispatch `h`. If the phaser's work count is already zero, `h` is dispatched immediately.

    template <class Handler>
    void post(Handler&& h);
    // Same as dispatch, but `h` is never dispatched immediately.

    void on_work_started() const noexcept;
    void on_work_finished() const noexcept;

private:
    template <class Handler>
    auto make_phase(Handler&& h);

    template <class Handler>
    struct ready_handler;

    template <class Handler>
    auto make_ready_handler(Handler&& h);

    template <class Handler>
    struct wait_handler;

    template <class Handler>
    auto make_wait_handler(Handler&& h);

    Executor executor;
    mutable boost::asio::steady_timer timer;
    mutable std::atomic<size_t> work_count{0};

    using time_point = boost::asio::steady_timer::clock_type::time_point;
};

// =============================================================================
// Inline implementation

template <class Executor>
template <class... Args>
phaser<Executor>::phaser(Args&&... args)
    : executor(std::forward<Args>(args)...)
    , timer(executor.get_io_service(), time_point::min())
{}

template <class Executor>
template <class Handler>
void phaser<Executor>::dispatch(Handler&& h) {
    executor.dispatch(make_phase(std::forward<Handler>(h)));
}

template <class Executor>
template <class Handler>
void phaser<Executor>::post(Handler&& h) {
    executor.post(make_phase(std::forward<Handler>(h)));
}

template <class Executor>
void phaser<Executor>::on_work_started() const noexcept {
    if (++work_count == 1) {
        executor.dispatch([this] {
            if (timer.expires_at() == time_point::min()) {
                boost::system::error_code ec;
                util::log::Logger lg;
                //BOOST_LOG(lg) << "in phase";
                auto cancelled = timer.expires_at(time_point::max(), ec);
                BOOST_ASSERT(!ec && !cancelled);
            }
        });
    }
}

template <class Executor>
void phaser<Executor>::on_work_finished() const noexcept {
    BOOST_ASSERT(work_count.load() != 0);
    if (--work_count == 0) {
        executor.dispatch([this] {
            boost::system::error_code ec;
            if (timer.cancel_one(ec) == 0) {
                BOOST_ASSERT(!ec);
                util::log::Logger lg;
                //BOOST_LOG(lg) << "out of phase";
                auto cancelled = timer.expires_at(time_point::min(), ec);
                BOOST_ASSERT(!cancelled);
            }
            else {
                util::log::Logger lg;
                //BOOST_LOG(lg) << "next phase";
            }
            BOOST_ASSERT(!ec);
        });
    }
}

template <class Executor>
template <class Handler>
auto phaser<Executor>::make_phase(Handler&& h) {
    return [this, h = decay_copy(std::forward<Handler>(h))] {
        util::log::Logger lg;
        if (timer.expires_at() == time_point::min()) {
            //BOOST_LOG(lg) << "dispatch invokes immediately";
            auto rh = make_ready_handler(std::move(h));
            using boost::asio::asio_handler_invoke;
            asio_handler_invoke(rh, &rh);
        }
        else {
            //BOOST_LOG(lg) << "dispatch defers";
            timer.async_wait(make_wait_handler(std::move(h)));
        }
    };
}

template <class Executor>
template <class Handler>
struct phaser<Executor>::ready_handler {
    work_guard<phaser> work;
    Handler h;

    void operator()(const boost::system::error_code& = {}) {
        h();
    }

    using logger_type = associated_logger_t<Handler>;
    logger_type get_logger() const { return get_associated_logger(h); }

    friend void* asio_handler_allocate(size_t size, ready_handler* self) {
        using boost::asio::asio_handler_allocate;
        return asio_handler_allocate(size, &self->h);
    }

    friend void asio_handler_deallocate(void* pointer, size_t size, ready_handler* self) {
        using boost::asio::asio_handler_deallocate;
        asio_handler_deallocate(pointer, size, &self->h);
    }

    template <class Function>
    friend void asio_handler_invoke(Function&& f, ready_handler* self) {
        using boost::asio::asio_handler_invoke;
        asio_handler_invoke(std::forward<Function>(f), &self->h);
    }

    friend bool asio_handler_is_continuation(ready_handler* self) {
        using boost::asio::asio_handler_is_continuation;
        return asio_handler_is_continuation(&self->h);
    }
};

template <class Executor>
template <class Handler>
auto phaser<Executor>::make_ready_handler(Handler&& h) {
    return ready_handler<std::decay_t<Handler>>{make_work_guard(*this), std::forward<Handler>(h)};
}

template <class Executor>
template <class Handler>
struct phaser<Executor>::wait_handler {
    phaser& parent;
    Handler h;

    void operator()(const boost::system::error_code& = {}) {
        auto rh = parent.make_ready_handler(std::move(h));
        asio_handler_invoke(rh, &rh);
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

template <class Executor>
template <class Handler>
auto phaser<Executor>::make_wait_handler(Handler&& h) {
    return wait_handler<std::decay_t<Handler>>{*this, std::forward<Handler>(h)};
}

}  // composed

#endif
