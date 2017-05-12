// Copyright (c) 2017 Barobo, Inc.
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#ifndef COMPOSED_RPC_MULTIPLEXER_HPP
#define COMPOSED_RPC_MULTIPLEXER_HPP

#include <composed/future.hpp>
#include <composed/op.hpp>

#include <chrono>
#include <map>

#include <boost/asio/yield.hpp>

namespace composed {

template <class RpcReplyType>
class rpc_multiplexer {
public:
    class transaction;

    void fulfill(uint32_t id, const RpcReplyType& reply) {
        auto request = transactions.find(id);
        if (request != transactions.end()) {
            request->second->emplace(reply);
        }
    }

private:
    uint32_t allocate_transaction(composed::future<RpcReplyType>& f) {
        auto id = next_transaction_id++;
        transactions[id] = &f;
        return id;
    }

    void deallocate_transaction(uint32_t id) {
        transactions.erase(id);
    }

    std::map<uint32_t, composed::future<RpcReplyType>*> transactions;
    uint32_t next_transaction_id = 0;
};

template <class RpcReplyType>
class rpc_multiplexer<RpcReplyType>::transaction {
public:
    transaction(boost::asio::io_service& context, rpc_multiplexer& m)
        : future_reply(context)
        , mux(&m)
        , id_(mux->allocate_transaction(future_reply))
    {}

    transaction(transaction&&) = delete;
    transaction& operator=(transaction&&) = delete;
    // Non-movable, because the rpc_multiplexer needs to be able to store our future's address.

    ~transaction() {
        BOOST_ASSERT(mux);
        mux->deallocate_transaction(id_);
    }

    void reset() {
        BOOST_ASSERT(mux);
        mux->deallocate_transaction(id_);
        id_ = mux->allocate_transaction(future_reply);
    }

    template <class Duration, class Token>
    auto async_wait_for(pb_size_t tag, Duration&& duration, Token&& token) {
        operation<wait_op<>>{}(*this,
                tag, std::forward<Duration>(duration), std::forward<Token>(token));
    }

    auto& future() { return future_reply; }
    const auto& future() const { return future_reply; }

    uint32_t id() { return id_; }

private:
    template <class Handler = void(boost::system::error_code)>
    struct wait_op;

    composed::future<RpcReplyType> future_reply;
    rpc_multiplexer* mux;
    uint32_t id_;
};

template <class RpcReplyType>
template <class Handler>
struct rpc_multiplexer<RpcReplyType>::transaction::
wait_op: boost::asio::coroutine {
    using handler_type = Handler;
    using allocator_type = beast::handler_alloc<char, handler_type>;
    using executor_type = composed::handler_executor<handler_type>;

    using logger_type = composed::logger;
    logger_type get_logger() const { return &lg; }

    transaction& self;
    pb_size_t tag;
    std::chrono::nanoseconds duration;

    mutable util::log::Logger lg;
    boost::system::error_code ec;

    template <class Duration>
    wait_op(handler_type& h, transaction& s, pb_size_t t, Duration d)
        : self(s)
        , tag(t)
        , duration(d)
    {}

    void operator()(composed::op<wait_op>&);
};

template <class RpcReplyType>
template <class Handler>
void
rpc_multiplexer<RpcReplyType>::transaction::wait_op<Handler>::
operator()(composed::op<wait_op>& op) {
    if (!ec) reenter(this) {
        yield return self.future().async_wait_for(duration, op(ec));
        if (self.future().value().which_arg != tag) {
            ec = boost::asio::error::network_down;
        }
    }
    op.complete(ec);
}

}  // composed

#include <boost/asio/unyield.hpp>

#endif