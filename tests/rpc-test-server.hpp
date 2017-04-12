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
#include <composed/async_rpc_read_loop.hpp>
#include <composed/handler_executor.hpp>

#include <beast/websocket.hpp>
#include <beast/core/handler_alloc.hpp>
#include <boost/asio.hpp>

#include <boost/asio/yield.hpp>

#include <queue>

// =======================================================================================
// Server operation

template <class Handler = void()>
struct server_op: boost::asio::coroutine {
    using handler_type = Handler;
    using allocator_type = beast::handler_alloc<char, handler_type>;

    using logger_type = composed::logger;
    logger_type get_logger() const { return &lg; }

    beast::websocket::stream<boost::asio::ip::tcp::socket>& ws;
    composed::phaser<composed::handler_executor<handler_type>> write_phaser;

    rpc_test_ClientToServer clientToServer{};

    float propertyValue = 1.0;

    mutable util::log::Logger lg;
    boost::system::error_code ec;

    // ===================================================================================
    // RPC request implementations

    template <class H>
    void request(const rpc_test_GetProperty_In& in, H&& handler);
    template <class H>
    void request(const rpc_test_SetProperty_In& in, H&& handler);

    // ===================================================================================
    // Event messages

    template <class H>
    void event(const rpc_test_RpcRequest& rpcRequest, H&& handler);
    template <class H>
    void event(const rpc_test_Quux&, H&& handler);

    server_op(handler_type& h, beast::websocket::stream<boost::asio::ip::tcp::socket>& stream,
            boost::asio::ip::tcp::endpoint remoteEp)
        : ws(stream)
        , write_phaser(stream.get_io_service(), h)
    {
        lg.add_attribute("Role", boost::log::attributes::make_constant("server"));
        lg.add_attribute("TcpRemoteEndpoint", boost::log::attributes::make_constant(remoteEp));
    }

    void operator()(composed::op<server_op>&);

private:
    template <class H = void()>
    struct reply_op;

    template <class T, class Token>
    auto async_reply(const T& message, Token&& token) {
        return composed::operation<reply_op<>>{}(*this, message, std::forward<Token>(token));
    }
};

template <class Handler>
template <class H>
struct server_op<Handler>::reply_op: boost::asio::coroutine {
    using handler_type = H;
    using allocator_type = beast::handler_alloc<char, handler_type>;

    server_op& self;
    composed::work_guard<composed::phaser<composed::handler_executor<server_op::handler_type>>> work;

    beast::basic_streambuf<allocator_type> buf;

    composed::associated_logger_t<handler_type> lg;
    boost::system::error_code ec;

    template <class T>
    reply_op(handler_type& h, server_op& s, const T& message)
        : self(s)
        , buf(256, allocator_type(h))
        , lg(composed::get_associated_logger(h))
    {
        auto serverToClient = rpc_test_ServerToClient{};
        nanopb::assign(serverToClient.arg.rpcReply.arg, message);
        nanopb::assign(serverToClient.arg, serverToClient.arg.rpcReply);
        auto success = nanopb::encode(buf, serverToClient);
        BOOST_ASSERT(success);
    }

    void operator()(composed::op<reply_op>& op);
};

template <class Handler>
template <class H>
void server_op<Handler>::reply_op<H>::
operator()(composed::op<reply_op>& op) {
    if (!ec) reenter(this) {
        yield return self.write_phaser.dispatch(op());
        work = composed::make_work_guard(self.write_phaser);
        yield return self.ws.async_write(buf.data(), op(ec));
    }
    else {
        BOOST_LOG(lg) << "reply error: " << ec.message();
    }
    op.complete();
}

template <class Handler>
template <class H>
inline void server_op<Handler>::request(const rpc_test_GetProperty_In& in, H&& handler) {
    BOOST_LOG(lg) << "received a GetProperty RPC request";
    this->async_reply(rpc_test_GetProperty_Out{true, propertyValue}, std::forward<H>(handler));
}

template <class Handler>
template <class H>
inline void server_op<Handler>::request(const rpc_test_SetProperty_In& in, H&& handler) {
    if (in.has_value) {
        propertyValue = in.value;
    }
    else {
        BOOST_LOG(lg) << "received an invalid SetProperty RPC request";
    }

    this->async_reply(rpc_test_GetProperty_Out{}, std::forward<H>(handler));
}

template <class Handler>
template <class H>
inline void server_op<Handler>::event(const rpc_test_RpcRequest& rpcRequest, H&& handler) {
    using composed::make_work_guard;

    auto serverToClient = rpc_test_ServerToClient{};
    serverToClient.arg.rpcReply.has_requestId = rpcRequest.has_requestId;
    serverToClient.arg.rpcReply.requestId = rpcRequest.requestId;

    auto h = handler;
    auto visitor = [this, h](const auto& req) mutable { this->request(req, std::move(h)); };
    if (!nanopb::visit(visitor, rpcRequest.arg)) {
        BOOST_LOG(lg) << "received an unrecognized RPC request";
        ws.get_io_service().post(std::move(h));
    }
}

template <class Handler>
template <class H>
inline void server_op<Handler>::event(const rpc_test_Quux&, H&& handler) {
    BOOST_LOG(lg) << "received a Quux event";
    ws.get_io_service().post(std::forward<H>(handler));
}

template <class Handler>
void server_op<Handler>::operator()(composed::op<server_op>& op) {
    if (!ec) reenter(this) {
        yield return ws.async_accept(op(ec));

        BOOST_LOG(lg) << "accepted WebSocket connection";

        ws.set_option(beast::websocket::message_type{beast::websocket::opcode::binary});

        yield return composed::async_rpc_read_loop(ws, clientToServer, *this, op(ec));

#if 0
        ws.next_layer().close(ec_read);  // ignored
        BOOST_LOG(lg) << "waiting for write loop";
        yield return write_phaser.dispatch(op());
        BOOST_LOG(lg) << "write loop ended";
#endif
    }
    else if (ec == beast::websocket::error::closed) {
        BOOST_LOG(lg) << "WebSocket closed, remote gave code/reason: "
                << ws.reason().code << '/' << ws.reason().reason.c_str();
    }
    else {
        BOOST_LOG(lg) << "error: " << ec.message();
    }
    op.complete();
}

constexpr composed::operation<server_op<>> async_server;

#include <boost/asio/unyield.hpp>

#endif
