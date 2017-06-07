// TODO:
// - use severity logging
// - use plf::colony

#ifndef RPC_TEST_SERVER_HPP
#define RPC_TEST_SERVER_HPP

#include <rpc-test.pb.hpp>
#include <pb_asio.hpp>

#include <util/log.hpp>

#include <composed/op.hpp>
#include <composed/phaser.hpp>
#include <composed/work_guard.hpp>
#include <composed/phased_stream.hpp>
#include <composed/handler_executor.hpp>
#include <composed/discard_results.hpp>

#include <beast/websocket.hpp>
#include <beast/core/handler_alloc.hpp>
#include <boost/asio.hpp>

#include <boost/log/attributes/scoped_attribute.hpp>

#include <boost/asio/yield.hpp>

// =======================================================================================
// Server operation

template <class Handler = void()>
struct server_op: boost::asio::coroutine {
    using handler_type = Handler;
    using allocator_type = beast::handler_alloc<char, handler_type>;
    using executor_type = composed::handler_executor<handler_type>;

    using logger_type = composed::logger;
    logger_type get_logger() const { return &lg; }

    composed::websocket<executor_type> ws;
    composed::rpc_stream<composed::websocket<executor_type>&, rpc_test_ClientToServer> stream;

    float propertyValue = 1.0;

    mutable util::log::Logger lg;
    boost::system::error_code ec;

    server_op(handler_type& h, boost::asio::ip::tcp::socket& s,
            boost::asio::ip::tcp::endpoint remote_ep)
        : ws(h, s)
        , stream(ws)
    {
        lg.add_attribute("Role", boost::log::attributes::make_constant("server"));
        lg.add_attribute("TcpRemoteEndpoint", boost::log::attributes::make_constant(remote_ep));
    }

    void operator()(composed::op<server_op>&);

    // ===================================================================================
    // Event messages

    template <class H>
    void event(const rpc_test_RpcRequest& rpcRequest, H&& handler);
    template <class H>
    void event(const rpc_test_Quux&, H&& handler);

private:
    // ===================================================================================
    // RPC request implementations

    template <class H>
    void request(uint32_t rid, const rpc_test_GetProperty_In& in, H&& handler);
    template <class H>
    void request(uint32_t rid, const rpc_test_SetProperty_In& in, H&& handler);

    template <class T, class Token>
    auto async_write_event(const T& message, Token&& token) {
        auto serverToClient = rpc_test_ServerToClient{};
        nanopb::assign(serverToClient.arg, message);
        return stream.async_write(serverToClient, std::forward<Token>(token));
    }

    template <class T, class Token>
    auto async_write_reply(uint32_t rid, const T& message, Token&& token) {
        auto reply = rpc_test_RpcReply{};
        reply.has_requestId = true;
        reply.requestId = rid;
        nanopb::assign(reply.arg, message);
        return async_write_event(reply, std::forward<Token>(token));
    }
};

// =======================================================================================
// Inline implementation

template <class Handler>
void server_op<Handler>::operator()(composed::op<server_op>& op) {
    if (!ec) reenter(this) {
        yield return stream.next_layer().next_layer().async_accept(op(ec));

        BOOST_LOG(lg) << "WebSocket accepted";

        stream.next_layer().next_layer().set_option(beast::websocket::message_type{beast::websocket::opcode::binary});

        yield return async_write_event(rpc_test_Quux{}, op(ec));
        BOOST_LOG(lg) << "sent a Quux";

        yield return stream.async_run_read_loop(*this, op(ec));
    }
    else if (ec == beast::websocket::error::closed) {
        BOOST_LOG(lg) << "WebSocket closed, remote gave code/reason: "
                << stream.next_layer().next_layer().reason().code << '/'
                << stream.next_layer().next_layer().reason().reason.c_str();
    }
    else {
        BOOST_LOG(lg) << "error: " << ec.message();
    }
    op.complete();
}

constexpr composed::operation<server_op<>> async_server;

template <class Handler>
template <class H>
inline void server_op<Handler>::event(const rpc_test_RpcRequest& rpcRequest, H&& handler) {
    if (!rpcRequest.has_requestId) {
        BOOST_LOG(lg) << "Received an RPC request without a request ID";
        stream.get_io_service().post(std::forward<H>(handler));
        return;
    }
    BOOST_LOG_SCOPED_LOGGER_TAG(lg, "RequestId", std::to_string(rpcRequest.requestId));
    auto visitor = [this, handler, rid = rpcRequest.requestId](const auto& req) mutable {
        this->request(rid, req, std::move(handler));
    };
    if (!nanopb::visit(visitor, rpcRequest.arg)) {
        BOOST_LOG(lg) << "received an unrecognized RPC request";
        stream.get_io_service().post(std::forward<H>(handler));
    }
}

template <class Handler>
template <class H>
inline void server_op<Handler>::event(const rpc_test_Quux&, H&& handler) {
    BOOST_LOG(lg) << "received a Quux event";
    stream.get_io_service().post(std::forward<H>(handler));
}

template <class Handler>
template <class H>
inline void server_op<Handler>::request(uint32_t rid, const rpc_test_GetProperty_In& in, H&& handler) {
    BOOST_LOG(lg) << "received a GetProperty RPC request";
    this->async_write_reply(rid, rpc_test_GetProperty_Out{true, propertyValue},
            composed::discard_results(std::forward<H>(handler)));
}

template <class Handler>
template <class H>
inline void server_op<Handler>::request(uint32_t rid, const rpc_test_SetProperty_In& in, H&& handler) {
    BOOST_LOG(lg) << "received a SetProperty RPC request";
    if (in.has_value) {
        propertyValue = in.value;
    }
    else {
        BOOST_LOG(lg) << "received an invalid SetProperty RPC request";
    }

    this->async_write_reply(rid, rpc_test_SetProperty_Out{},
            composed::discard_results(std::forward<H>(handler)));
}

#include <boost/asio/unyield.hpp>

#endif
