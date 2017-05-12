// Copyright (c) 2017 Barobo, Inc.
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#ifndef COMPOSED_RPC_CLIENT_HPP
#define COMPOSED_RPC_CLIENT_HPP

#include <composed/rpc_multiplexer.hpp>
#include <composed/op.hpp>
#include <composed/phaser.hpp>

#include <chrono>
#include <map>

#include <boost/asio/yield.hpp>

namespace composed {

// =======================================================================================
// RPC Client

template <class Derived, class RpcRequestType, class RpcReplyType>
class rpc_client {
public:
    using client_base_type = rpc_client;
    // A little help for our derived class to refer to us.

    class transaction;

    // ===================================================================================
    // Event messages

    template <class H>
    void event(const RpcReplyType& rpcReply, H&& handler);

private:
    composed::rpc_multiplexer<RpcReplyType> muxer;
};

template <class Derived, class RpcRequestType, class RpcReplyType>
template <class H>
void rpc_client<Derived, RpcRequestType, RpcReplyType>::
event(const RpcReplyType& rpcReply, H&& handler) {
    auto lg = get_associated_logger(handler);
    if (!rpcReply.has_requestId) {
        BOOST_LOG(lg) << "Received an RPC reply without a request ID";
        static_cast<Derived*>(this)->get_io_service().post(std::forward<H>(handler));
    }
    else {
        BOOST_LOG_SCOPED_LOGGER_TAG(lg, "RequestId", std::to_string(rpcReply.requestId));
        BOOST_LOG(lg) << "Fulfilling request";
        muxer.fulfill(rpcReply.requestId, rpcReply);
        static_cast<Derived*>(this)->get_io_service().post(std::forward<H>(handler));
    }
}

template <class Derived, class RpcRequestType, class RpcReplyType>
class rpc_client<Derived, RpcRequestType, RpcReplyType>::transaction {
public:
    explicit transaction(rpc_client& c)
            : inner(static_cast<Derived&>(c).get_io_service(), c.muxer)
            , client(c)
    {}

private:
    using inner_transaction = typename decltype(rpc_client::muxer)::transaction;
    inner_transaction inner;
    rpc_client& client;

    template <class H = void(boost::system::error_code)>
    struct do_request_op;

public:

    void reset() { inner.reset(); }

    const RpcReplyType& reply() const {
        return inner.future().value();
    }

    template <class T, class Duration, class Token>
    auto async_do_request(
            const T& message, pb_size_t expected_tag, Duration&& duration, Token&& token) {
        return composed::operation<do_request_op<>>{}(*this, message, expected_tag,
                std::forward<Duration>(duration), std::forward<Token>(token));
    }
};

template <class Derived, class RpcRequestType, class RpcReplyType>
template <class H>
struct rpc_client<Derived, RpcRequestType, RpcReplyType>::transaction::
do_request_op: boost::asio::coroutine {
    using handler_type = H;
    using allocator_type = beast::handler_alloc<char, handler_type>;

    using logger_type = composed::logger;
    logger_type get_logger() const { return &lg; }

    transaction& self;

    RpcRequestType request;

    pb_size_t expected_tag;
    std::chrono::nanoseconds duration;

    mutable util::log::Logger lg;
    boost::system::error_code ec;

    template <class T, class Duration>
    do_request_op(handler_type& h, transaction& s, const T& message, pb_size_t t, Duration&& d)
        : self(s)
        , expected_tag(t)
        , duration(std::forward<Duration>(d))
        , lg(composed::get_associated_logger(h).clone())

    {
        request.has_requestId = true;
        request.requestId = self.inner.id();
        nanopb::assign(request.arg, message);
        lg.add_attribute("RequestId",
                boost::log::attributes::make_constant(std::to_string(self.inner.id())));
        lg.add_attribute("RequestName",
                boost::log::attributes::make_constant(boost::typeindex::type_id<T>().pretty_name()));
    }

    void operator()(composed::op<do_request_op>& op);
};

template <class Derived, class RpcRequestType, class RpcReplyType>
template <class H>
void rpc_client<Derived, RpcRequestType, RpcReplyType>::transaction::do_request_op<H>::
operator()(composed::op<do_request_op>& op) {
    if (!ec) reenter(this) {
        yield return static_cast<Derived&>(self.client).async_write_event(request, op(ec));

        BOOST_LOG(lg) << "sent request, awaiting reply";

        yield return self.inner.async_wait_for(expected_tag, duration, op(ec));
    }
    op.complete(ec);
}

}  // composed

#include <boost/asio/unyield.hpp>

#endif
