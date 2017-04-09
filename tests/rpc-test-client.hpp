#ifndef RPC_TEST_CLIENT_HPP
#define RPC_TEST_CLIENT_HPP

#include <rpc-test.pb.hpp>
#include <pb_asio.hpp>

#include <util/log.hpp>

#include <composed/op.hpp>

#include <beast/websocket.hpp>
#include <beast/core/handler_alloc.hpp>
#include <boost/asio.hpp>

#include <boost/asio/yield.hpp>

struct TestClient {
    //util::RpcRequestBank<rpc_test_RpcRequest, rpc_test_RpcReply> requestBank;

    util::log::Logger lg;

    // ===================================================================================
    // Event messages

    void operator()(const rpc_test_RpcReply& rpcReply);
    void operator()(const rpc_test_Quux& quux);
};

void TestClient::operator()(const rpc_test_RpcReply& rpcReply) {
    if (rpcReply.has_requestId) {
        #if 0
        if (!nanopb::visit(requestBank[rpcReply.requestId], rpcReply.arg)) {
            BOOST_LOG(lg) << "Client received an RPC reply with unexpected type";
        }
        #endif
    }
    else {
        BOOST_LOG(lg) << "Server received an RPC reply with no request ID";
    }
}

void TestClient::operator()(const rpc_test_Quux& quux) {
    BOOST_LOG(lg) << "Client received a Quux event";
}


// =======================================================================================
// Client operation

template <class Handler = void()>
struct client_op: boost::asio::coroutine {
    using handler_type = Handler;
    using allocator_type = beast::handler_alloc<char, handler_type>;
    using logger_type = composed::logger;
    logger_type get_logger() const { return &lg; }

    TestClient client;
    beast::websocket::stream<boost::asio::ip::tcp::socket>& ws;
    boost::asio::ip::tcp::endpoint serverEndpoint;
    beast::websocket::opcode opcode;
    beast::basic_streambuf<allocator_type> buf;
    rpc_test_ClientToServer clientToServer;

    util::log::Logger lg;
    boost::system::error_code ec;
    boost::system::error_code ec_read;

    void event(const rpc_test_RpcReply& rpcReply);
    void event(const rpc_test_Quux&);

    client_op(handler_type& h, beast::websocket::stream<boost::asio::ip::tcp::socket>& stream,
            boost::asio::ip::tcp::endpoint ep)
        : ws(stream)
        , serverEndpoint(ep)
        , buf(256, allocator_type(h))
        , clientToServer{}
    {
        lg.add_attribute("Role", boost::log::attributes::make_constant("client"));
        lg.add_attribute("TcpRemoteEndpoint", boost::log::attributes::make_constant(serverEndpoint));
    }

    void operator()(composed::op<client_op>&);
};

template <class Handler>
void client_op<Handler>::event(const rpc_test_RpcReply& rpcReply) {
    BOOST_LOG(lg) << "Received an RPC reply";
}

template <class Handler>
void client_op<Handler>::event(const rpc_test_Quux& quux) {
    BOOST_LOG(lg) << "Received a Quux";
}

template <class Handler>
void client_op<Handler>::operator()(composed::op<client_op>& op) {
    if (!ec) reenter(this) {
        yield return ws.next_layer().async_connect(serverEndpoint, op(ec));
        yield return ws.async_handshake("hodorhodorhodor.com", "/cgi-bin/hax", op(ec));

        BOOST_LOG(lg) << "established WebSocket connection";

        ws.set_option(beast::websocket::message_type{beast::websocket::opcode::binary});

        yield {
            clientToServer.arg.quux.value = 666;
            nanopb::assign(clientToServer.arg, clientToServer.arg.quux);
            auto ostream = nanopb::ostream_from_dynamic_buffer(buf);
            if (!nanopb::encode(ostream, clientToServer)) {
                BOOST_LOG(lg) << "encoding failure";
                buf.consume(buf.size());
                ec = boost::asio::error::operation_aborted;
                break;
            }
            return ws.async_write(buf.data(), op(ec));
        }
        buf.consume(buf.size());

        yield {
            nanopb::assign(clientToServer.arg.rpcRequest.arg, clientToServer.arg.rpcRequest.arg.getProperty);
            nanopb::assign(clientToServer.arg, clientToServer.arg.rpcRequest);
            auto ostream = nanopb::ostream_from_dynamic_buffer(buf);
            if (!nanopb::encode(ostream, clientToServer)) {
                BOOST_LOG(lg) << "encoding failure";
                buf.consume(buf.size());
                ec = boost::asio::error::operation_aborted;
                break;
            }
            return ws.async_write(buf.data(), op(ec));
        }
        buf.consume(buf.size());

        do {
            while (buf.size()) {
                BOOST_LOG(lg) << buf.size() << " bytes in buffer";
                auto serverToClient = rpc_test_ServerToClient{};
                auto istream = nanopb::istream_from_dynamic_buffer(buf);
                if (!nanopb::decode(istream, serverToClient)) {
                    BOOST_LOG(lg) << "decoding error";
                }
                else {
                    nanopb::visit([this](const auto& x) { this->event(x); }, serverToClient.arg);
                }
            }
            BOOST_LOG(lg) << "reading ...";
            yield return ws.async_read(opcode, buf, op(ec_read));
        } while (!ec_read);

        BOOST_LOG(lg) << "closing connection";
        yield return ws.async_close({"Hodor!"}, op(ec));

        do {
            BOOST_LOG(lg) << "reading ...";
            buf.consume(buf.size());
            yield return ws.async_read(opcode, buf, op(ec_read));
        } while (!ec_read);

        if (ec_read == beast::websocket::error::closed) {
            BOOST_LOG(lg) << "WebSocket closed, remote gave code/reason: "
                    << ws.reason().code << '/' << ws.reason().reason.c_str();
        }
        else {
            BOOST_LOG(lg) << "read error: " << ec_read.message();
        }

    }
    else {
        BOOST_LOG(lg) << "error: " << ec.message();
    }
    op.complete();
}

constexpr composed::operation<client_op<>> async_client;

#include  <boost/asio/unyield.hpp>

#endif
