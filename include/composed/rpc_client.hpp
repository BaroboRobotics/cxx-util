// Copyright (c) 2017 Barobo, Inc.
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#ifndef COMPOSED_RPC_CLIENT_HPP
#define COMPOSED_RPC_CLIENT_HPP

#include <composed/op.hpp>
#include <composed/phaser.hpp>
#include <composed/future.hpp>

#include <boost/log/attributes/scoped_attribute.hpp>

#include <boost/type_index.hpp>

#include <chrono>
#include <map>

#include <boost/asio/yield.hpp>

namespace composed {

// =======================================================================================
// RPC Client

template <class RpcStream, class RpcRequestType, class RpcReplyType>
class rpc_client {
public:
    template <class... Args>
    explicit rpc_client(Args&&... args): next_layer_(std::forward<Args>(args)...) {}

    class transactor;

    boost::asio::io_service& get_io_service() { return next_layer_.get_io_service(); }
    auto& next_layer() { return next_layer_; }
    const auto& next_layer() const { return next_layer_; }
    auto& lowest_layer() { return next_layer_.lowest_layer(); }
    const auto& lowest_layer() const { return next_layer_.lowest_layer(); }

    // ===================================================================================
    // Event messages

    void event(const RpcReplyType& e);

private:
    uint32_t allocate_transaction(composed::future<RpcReplyType>& t) {
        auto id = next_transaction_id++;
        transactions[id] = &t;
        return id;
    }

    void deallocate_transaction(uint32_t id) {
        transactions.erase(id);
    }

    RpcStream next_layer_;
    std::map<uint32_t, composed::future<RpcReplyType>*> transactions;
    uint32_t next_transaction_id = 0;
};

template <class RpcStream, class RpcRequestType, class RpcReplyType>
void rpc_client<RpcStream, RpcRequestType, RpcReplyType>::
event(const RpcReplyType& e) {
    if (e.has_requestId) {
        auto request = transactions.find(e.requestId);
        if (request != transactions.end()) {
            request->second->emplace(e);
        }
    }
}

// =======================================================================================
// RPC Client transactor

template <class RpcStream, class RpcRequestType, class RpcReplyType>
class rpc_client<RpcStream, RpcRequestType, RpcReplyType>::transactor {
    rpc_client& client;
    composed::future<RpcReplyType> transaction;
    boost::asio::io_service::strand strand;
    composed::phaser<boost::asio::io_service::strand&> phaser;
    uint32_t id_;

public:
    explicit transactor(rpc_client& c)
            : client(c)
            , transaction(client.next_layer_.get_io_service())
            , strand(client.next_layer_.get_io_service())
            , phaser(strand)
            , id_(client.allocate_transaction(transaction))
    {}

    transactor(transactor&&) = delete;
    transactor& operator=(transactor&&) = delete;
    // Non-movable, because the rpc_client needs to be able to store our future's address.

    ~transactor() {
        client.deallocate_transaction(id_);
    }

    boost::asio::io_service& get_io_service() { return client.next_layer_.get_io_service(); }
    auto& next_layer() { return client.next_layer_; }
    const auto& next_layer() const { return client.next_layer_; }
    auto& lowest_layer() { return client.next_layer_.lowest_layer(); }
    const auto& lowest_layer() const { return client.next_layer_.lowest_layer(); }

    void close(boost::system::error_code& ec) {
        transaction.close(ec);
    }

    using work_guard = composed::work_guard<decltype(phaser)>;

private:
    template <class Handler = void(boost::system::error_code, work_guard)>
    struct do_request_op;

    void reallocate() {
        client.deallocate_transaction(id_);
        id_ = client.allocate_transaction(transaction);
    }

public:

    template <class T, class Duration, class Token>
    auto async_do_request(
            const T& message, pb_size_t expected_tag, Duration&& duration, Token&& token) {
        return composed::operation<do_request_op<>>{}(*this, message, expected_tag,
                std::forward<Duration>(duration), std::forward<Token>(token));
    }

    const auto& reply() const {
        return transaction.value().arg;
    }

    const auto& replyEx() const {
        return transaction.value();
    }
};

template <class RpcStream, class RpcRequestType, class RpcReplyType>
template <class H>
struct rpc_client<RpcStream, RpcRequestType, RpcReplyType>::transactor::
do_request_op: boost::asio::coroutine {
    using handler_type = H;
    using allocator_type = beast::handler_alloc<char, handler_type>;

    using logger_type = composed::logger;
    logger_type get_logger() const { return &lg; }

    transactor& self;

    RpcRequestType request;

    pb_size_t expected_tag;
    std::chrono::nanoseconds duration;

    composed::work_guard<decltype(self.phaser)> work;

    mutable util::log::Logger lg;
    boost::system::error_code ec;

    template <class T, class Duration>
    do_request_op(handler_type& h, transactor& s, const T& message, pb_size_t t, Duration&& d)
        : self(s)
        , expected_tag(t)
        , duration(std::forward<Duration>(d))
        , lg(composed::get_associated_logger(h).clone())

    {
        nanopb::assign(request.arg, message);
        this->resetLogAttribute("RequestName", boost::log::attributes::make_constant(boost::typeindex::type_id<T>().pretty_name()));
    }

    template <class T>
    void resetLogAttribute(const char* key, const T& value) {
        // TODO: Very hacky and inefficient. :( Can we do better with a mutable attribute?
        auto attrs = lg.get_attributes();
        attrs.erase(key);
        lg.set_attributes(attrs);
        lg.add_attribute(key, value);
    }

    void operator()(composed::op<do_request_op>& op);
};

template <class RpcStream, class RpcRequestType, class RpcReplyType>
template <class H>
void rpc_client<RpcStream, RpcRequestType, RpcReplyType>::transactor::do_request_op<H>::
operator()(composed::op<do_request_op>& op) {
    if (!ec) reenter(this) {
        yield return self.phaser.dispatch(op());
        work = composed::make_work_guard(self.phaser);

        request.has_requestId = true;
        request.requestId = self.id_;
        this->resetLogAttribute("RequestId", boost::log::attributes::make_constant(std::to_string(self.id_)));
        // We must set these only after dispatching through the phaser, as otherwise we'll 
        // set the wrong requestId.

        yield return self.client.next_layer_.async_write(request, op(ec));
        BOOST_LOG(lg) << "sent request";

        yield return self.transaction.async_wait_for(duration, op(ec));
        if (self.transaction.value().which_arg != expected_tag) {
            ec = boost::asio::error::network_down;
            // FIXME
        }

        BOOST_LOG(lg) << "got reply";
    }
    self.reallocate();
    op.complete(ec, std::move(work));
}

}  // composed

#include <boost/asio/unyield.hpp>

#endif
