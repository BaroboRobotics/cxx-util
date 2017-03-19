// TODO:
// - use severity logging
// - use plf::colony

#ifndef RPC_TEST_SERVER_HPP
#define RPC_TEST_SERVER_HPP

#include <rpc-test.pb.hpp>
#include <pb_asio.hpp>

#include <util/log.hpp>

#include <composed/op_logger.hpp>

#include <beast/websocket.hpp>
#include <beast/core/handler_alloc.hpp>
#include <boost/asio.hpp>

#include <boost/asio/yield.hpp>

// =======================================================================================
// Server operation

template <class Handler = void()>
struct server_op: boost::asio::coroutine {
    using handler_type = Handler;
    using allocator_type = beast::handler_alloc<char, handler_type>;
    using logger_type = composed::logger;
    logger_type get_logger() const { return &lg; }

    beast::websocket::stream<boost::asio::ip::tcp::socket>& ws;
    beast::websocket::opcode opcode;
    beast::basic_streambuf<allocator_type> buf;

    float propertyValue = 1.0;

    util::log::Logger lg;
    boost::system::error_code ec;
    boost::system::error_code ecRead;

    // ===================================================================================
    // RPC request implementations

    rpc_test_GetProperty_Out request(const rpc_test_GetProperty_In& in);
    rpc_test_SetProperty_Out request(const rpc_test_SetProperty_In& in);

    // ===================================================================================
    // Event messages

    void event(const rpc_test_RpcRequest& rpcRequest);
    void event(const rpc_test_Quux&);

    server_op(handler_type& h, beast::websocket::stream<boost::asio::ip::tcp::socket>& stream,
            boost::asio::ip::tcp::endpoint remoteEp)
        : ws(stream)
        , buf(256, allocator_type(h))
    {
        lg.add_attribute("Role", boost::log::attributes::make_constant("server"));
        lg.add_attribute("TcpRemoteEndpoint", boost::log::attributes::make_constant(remoteEp));
    }

    void operator()(composed::op<server_op>&);
};

template <class Handler>
inline rpc_test_GetProperty_Out server_op<Handler>::request(const rpc_test_GetProperty_In& in) {
    BOOST_LOG(lg) << "received a GetProperty RPC request";
    return {true, propertyValue};
}

template <class Handler>
inline rpc_test_SetProperty_Out server_op<Handler>::request(const rpc_test_SetProperty_In& in) {
    if (in.has_value) {
        propertyValue = in.value;
    }
    else {
        BOOST_LOG(lg) << "received an invalid SetProperty RPC request";
    }
    return {};
}

template <class Handler>
inline void server_op<Handler>::event(const rpc_test_RpcRequest& rpcRequest) {
    auto serverToClient = rpc_test_ServerToClient{};
    serverToClient.arg.rpcReply.has_requestId = rpcRequest.has_requestId;
    serverToClient.arg.rpcReply.requestId = rpcRequest.requestId;

    auto visitor = [&serverToClient, this](const auto& req) {
        nanopb::assign(serverToClient.arg.rpcReply.arg, this->request(req));
        nanopb::assign(serverToClient.arg, serverToClient.arg.rpcReply);
    };
    if (nanopb::visit(visitor, rpcRequest.arg)) {
        BOOST_LOG(lg) << "received and replied to an RPC request";

    }
    else {
        BOOST_LOG(lg) << "received an unrecognized RPC request";
    }
}

template <class Handler>
inline void server_op<Handler>::event(const rpc_test_Quux&) {
    BOOST_LOG(lg) << "received a Quux event";
}

template <class Handler>
void server_op<Handler>::operator()(composed::op<server_op>& op) {
    if (!ec) reenter(this) {
        yield return ws.async_accept(op(ec));

        BOOST_LOG(lg) << "accepted WebSocket connection";

        ws.set_option(beast::websocket::message_type{beast::websocket::opcode::binary});

        do {
            while (buf.size()) {
                BOOST_LOG(lg) << buf.size() << " bytes in buffer";
                auto clientToServer = rpc_test_ClientToServer{};
                auto istream = nanopb::istream_from_dynamic_buffer(buf);
                if (!nanopb::decode(istream, clientToServer)) {
                    BOOST_LOG(lg) << "decoding error";
                }
                else {
                    nanopb::visit([this](const auto& x) { this->event(x); }, clientToServer.arg);
                }
            }
            BOOST_LOG(lg) << "reading ...";
            yield return ws.async_read(opcode, buf, op(ecRead));
        } while (!ecRead);

        BOOST_LOG(lg) << "read error: " << ecRead.message();

        // To close the connection, we're supposed to call (async_)close, then read until we get
        // an error.
        BOOST_LOG(lg) << "closing connection";
        yield return ws.async_close({"Hodor indeed!"}, op(ec));

        do {
            BOOST_LOG(lg) << "reading ...";
            buf.consume(buf.size());
            yield return ws.async_read(opcode, buf, op(ecRead));
        } while (!ecRead);

        if (ecRead == beast::websocket::error::closed) {
            BOOST_LOG(lg) << "WebSocket closed, remote gave code/reason: "
                    << ws.reason().code << '/' << ws.reason().reason.c_str();
        }
        else {
            BOOST_LOG(lg) << "read error: " << ecRead.message();
        }
    }
    else {
        BOOST_LOG(lg) << "error: " << ec.message();
    }
    op.complete();
}

#include <boost/asio/unyield.hpp>

#endif
