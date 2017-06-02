// Copyright (c) 2017 Barobo, Inc.
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#ifndef COMPOSED_HANDLER_EXECUTOR_HPP
#define COMPOSED_HANDLER_EXECUTOR_HPP

#include <composed/handler_context.hpp>

#include <boost/asio/io_service.hpp>

#include <utility>

namespace composed {

template <class Handler>
class handler_executor {
public:
    using handler_type = Handler;

    handler_executor(boost::asio::io_service& c, handler_type& h): context(c), handler(h) {}

    boost::asio::io_service& get_io_service() { return context; }

    template <class CompletionHandler>
    void post(CompletionHandler&& ch) const {
        context.post(bind_handler_context(handler, std::forward<CompletionHandler>(ch)));
    }

    template <class CompletionHandler>
    void dispatch(CompletionHandler&& ch) const {
        using boost::asio::asio_handler_invoke;
        asio_handler_invoke(std::forward<CompletionHandler>(ch), &handler);
    }

private:
    boost::asio::io_service& context;
    handler_type& handler;
};

}  // composed

#endif
