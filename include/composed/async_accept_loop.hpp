// Copyright (c) 2017 Barobo, Inc.
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#ifndef COMPOSED_ASYNC_ACCEPT_LOOP_HPP
#define COMPOSED_ASYNC_ACCEPT_LOOP_HPP

#include <composed/op.hpp>

#include <boost/asio/ip/tcp.hpp>

#include <boost/asio/yield.hpp>

#include <utility>

namespace composed {

template <class LoopBody, class Token>
auto async_accept_loop(boost::asio::ip::tcp::acceptor& a, LoopBody&& f, Token&& token);
// Continuously accept new connections, passing them to the user by `f(std::move(s), ep)`, where:
//   - `s` is a newly bound socket
//   - `ep` is the remote endpoint connected to that socket
//
// If the accept syscall results in an error, the loop terminates and propagates the error.
//
// Note that there is no way to break the loop from inside the loop body, `f`.
//
// TODO: figure out where something like this should live. Should it stay in namespace composed?

template <class LoopBody, class Handler = void(boost::system::error_code)>
struct accept_loop_op: boost::asio::coroutine {
    using handler_type = Handler;

    accept_loop_op(handler_type&, boost::asio::ip::tcp::acceptor& a, LoopBody f)
        : acceptor(a)
        , stream(a.get_io_service())
        , loop_body(std::move(f))
    {}

    void operator()(composed::op<accept_loop_op>&);

    boost::asio::ip::tcp::acceptor& acceptor;

    boost::asio::ip::tcp::socket stream;
    boost::asio::ip::tcp::endpoint remoteEp;

    LoopBody loop_body;

    boost::system::error_code ec;
};

template <class LoopBody, class Handler>
void accept_loop_op<LoopBody, Handler>::operator()(composed::op<accept_loop_op>& op) {
    reenter(this) {
        yield return acceptor.async_accept(stream, remoteEp, op(ec));
        while (!ec) {
            loop_body(std::move(stream), remoteEp);
            stream = decltype(stream)(acceptor.get_io_service());
            yield return acceptor.async_accept(stream, remoteEp, op(ec));
        }
    }
    op.complete(ec);
}

template <class LoopBody, class Token>
auto async_accept_loop(boost::asio::ip::tcp::acceptor& a, LoopBody&& f, Token&& token) {
    return composed::async_run<accept_loop_op<std::decay_t<LoopBody>>>(
            a, std::forward<LoopBody>(f), std::forward<Token>(token));
}

}  // composed

#include <boost/asio/unyield.hpp>

#endif