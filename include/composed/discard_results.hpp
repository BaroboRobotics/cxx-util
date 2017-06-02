// Copyright (c) 2017 Barobo, Inc.
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#ifndef COMPOSED_DISCARD_RESULTS_HPP
#define COMPOSED_DISCARD_RESULTS_HPP

#include <composed/associated_logger.hpp>

namespace composed {

template <class Handler>
struct discard_results_handler {
    Handler h;

    template <class... Args>
    void operator()(Args&& ...) {
        h();
    }

    using logger_type = associated_logger_t<Handler>;
    logger_type get_logger() const { return get_associated_logger(h); }

    friend void* asio_handler_allocate(size_t size, discard_results_handler* self) {
        using boost::asio::asio_handler_allocate;
        return asio_handler_allocate(size, &self->h);
    }

    friend void asio_handler_deallocate(void* pointer, size_t size, discard_results_handler* self) {
        using boost::asio::asio_handler_deallocate;
        asio_handler_deallocate(pointer, size, &self->h);
    }

    template <class Function>
    friend void asio_handler_invoke(Function&& f, discard_results_handler* self) {
        using boost::asio::asio_handler_invoke;
        asio_handler_invoke(std::forward<Function>(f), &self->h);
    }

    friend bool asio_handler_is_continuation(discard_results_handler* self) {
        using boost::asio::asio_handler_is_continuation;
        return asio_handler_is_continuation(&self->h);
    }

};

template <class Handler>
auto discard_results(Handler&& handler) {
    return discard_results_handler<std::decay_t<Handler>>{std::forward<Handler>(handler)};
}

}  // composed

#endif