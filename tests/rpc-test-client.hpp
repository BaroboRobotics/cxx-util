#ifndef RPC_TEST_CLIENT_HPP
#define RPC_TEST_CLIENT_HPP

#include <rpc-test.pb.hpp>
#include <pb_asio.hpp>

#include <util/log.hpp>

#include <composed/op.hpp>
#include <composed/async_rpc_read_loop.hpp>

#include <beast/websocket.hpp>
#include <beast/core/handler_alloc.hpp>
#include <boost/asio.hpp>

#include <boost/asio/yield.hpp>

// =======================================================================================
// Client operation

template <class Handler = void()>
struct client_op: boost::asio::coroutine {
    using handler_type = Handler;
    using allocator_type = beast::handler_alloc<char, handler_type>;

    using logger_type = composed::logger;
    logger_type get_logger() const { return &lg; }

    composed::phaser<composed::handler_executor<handler_type>> phaser;

    beast::websocket::stream<boost::asio::ip::tcp::socket>& ws;
    boost::asio::ip::tcp::endpoint serverEndpoint;
    beast::basic_streambuf<allocator_type> buf;

    rpc_test_ClientToServer clientToServer;
    rpc_test_ServerToClient serverToClient;

    mutable util::log::Logger lg;
    boost::system::error_code ec;

    template <class H>
    void event(const rpc_test_RpcReply& rpcReply, H&& handler);
    template <class H>
    void event(const rpc_test_Quux&, H&& handler);

    client_op(handler_type& h, beast::websocket::stream<boost::asio::ip::tcp::socket>& stream,
            boost::asio::ip::tcp::endpoint ep)
        : phaser(stream.get_io_service(), h)
        , ws(stream)
        , serverEndpoint(ep)
        , buf(256, allocator_type(h))
        , clientToServer{}
        , serverToClient{}
    {
        lg.add_attribute("Role", boost::log::attributes::make_constant("client"));
        lg.add_attribute("TcpRemoteEndpoint", boost::log::attributes::make_constant(serverEndpoint));
    }

    void operator()(composed::op<client_op>&);
};

template <class Handler>
template <class H>
void client_op<Handler>::event(const rpc_test_RpcReply& rpcReply, H&& handler) {
    BOOST_LOG(lg) << "Received an RPC reply";
    ws.get_io_service().post(std::forward<H>(handler));
}

template <class Handler>
template <class H>
void client_op<Handler>::event(const rpc_test_Quux& quux, H&& handler) {
    BOOST_LOG(lg) << "Received a Quux";
    ws.get_io_service().post(std::forward<H>(handler));
}

template <class Handler>
void client_op<Handler>::operator()(composed::op<client_op>& op) {
    if (!ec) reenter(this) {
        yield return ws.next_layer().async_connect(serverEndpoint, op(ec));
        yield return ws.async_handshake("hodorhodorhodor.com", "/cgi-bin/hax", op(ec));

        BOOST_LOG(lg) << "established WebSocket connection";

        ws.set_option(beast::websocket::message_type{beast::websocket::opcode::binary});

        {
            auto cleanup = op.wrap(
                    [this, work = make_work_guard(phaser)](const boost::system::error_code& ec) {
                        if (ec == beast::websocket::error::closed) {
                            BOOST_LOG(lg) << "WebSocket closed, remote code/reason: "
                                    << ws.reason().code << '/' << ws.reason().reason.c_str();
                        }
                        else {
                            BOOST_LOG(lg) << "read loop error: " << ec.message();
                        }
                    });
            composed::async_rpc_read_loop(ws, serverToClient, *this, std::move(cleanup));
        }

        // send a Quux
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
        BOOST_LOG(lg) << "sent a Quux";

        // send an RPC request
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
        BOOST_LOG(lg) << "sent an rpc request";

        BOOST_LOG(lg) << "closing connection";
        yield return ws.async_close({"Hodor!"}, op(ec));

        BOOST_LOG(lg) << "waiting for read loop";
        yield return phaser.dispatch(op());
        BOOST_LOG(lg) << "read loop done, client op exiting";
    }
    else {
        BOOST_LOG(lg) << "error: " << ec.message();
    }
    op.complete();
}

constexpr composed::operation<client_op<>> async_client;

#include  <boost/asio/unyield.hpp>

#endif
