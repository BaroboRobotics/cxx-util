// Copyright (c) 2017 Barobo, Inc.
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#ifndef COMPOSED_ASYNC_RPC_READ_LOOP_HPP
#define COMPOSED_ASYNC_RPC_READ_LOOP_HPP

#include <pb_asio.hpp>

#include <composed/op.hpp>

#include <beast/websocket.hpp>
#include <beast/core/handler_alloc.hpp>
#include <boost/asio.hpp>

#include <boost/asio/yield.hpp>

namespace composed {

// =======================================================================================
// rpc_read_loop

template <class T, class EventProc, class Handler = void(boost::system::error_code)>
struct rpc_read_loop_op: boost::asio::coroutine {
    using handler_type = Handler;
    using allocator_type = beast::handler_alloc<char, handler_type>;

    beast::websocket::stream<boost::asio::ip::tcp::socket>& ws;
    beast::websocket::opcode opcode;
    beast::basic_streambuf<allocator_type> buf;

    T message;
    EventProc& event_processor;

    composed::associated_logger_t<handler_type> lg;
    boost::system::error_code ec;

    rpc_read_loop_op(handler_type& h, beast::websocket::stream<boost::asio::ip::tcp::socket>& s,
            EventProc& ep)
        : ws(s)
        , buf(256, allocator_type(h))
        , event_processor(ep)
        , lg(composed::get_associated_logger(h))
    {}

    void operator()(composed::op<rpc_read_loop_op>& op);
};

template <class T, class EventProc, class Handler>
void rpc_read_loop_op<T, EventProc, Handler>::operator()(composed::op<rpc_read_loop_op>& op) {
    if (!ec) reenter(this) {
        while (true) {
            while (buf.size()) {
                message = {};
                if (nanopb::decode(buf, message)) {
                    yield {
                        auto h = op();
                        auto visitor = [this, h](const auto& x) {
                            event_processor.event(x, std::move(h));
                        };
                        if (!nanopb::visit(visitor, message.arg)) {
                            BOOST_LOG(lg) << "unrecognized message";
                            ws.get_io_service().post(std::move(h));
                        }
                        return;
                    }
                }
                else {
                    BOOST_LOG(lg) << "decoding error";
                }
            }
            yield return ws.async_read(opcode, buf, op(ec));
        }
    }
    op.complete(ec);
}

template <class T, class EventProc, class Token>
auto async_rpc_read_loop(beast::websocket::stream<boost::asio::ip::tcp::socket>& stream,
        EventProc& event_processor, Token&& token) {
    return composed::operation<rpc_read_loop_op<T, EventProc>>{}(
            stream, event_processor, std::forward<Token>(token));
}

}  // composed

#include <boost/asio/unyield.hpp>

#endif