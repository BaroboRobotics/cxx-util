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
#include <composed/handler_context.hpp>

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

    using executor_type = boost::asio::io_service::strand;
    executor_type& get_executor() { return strand; }

    using logger_type = composed::logger;
    logger_type get_logger() const { return &lg; }

    handler_type& handler_context;

    beast::websocket::stream<boost::asio::ip::tcp::socket>& ws;
    beast::websocket::opcode opcode;
    beast::basic_streambuf<allocator_type> ibuf;
    beast::basic_streambuf<allocator_type> obuf;

    boost::asio::io_service::strand strand;
    composed::phaser write_phaser;

    float propertyValue = 1.0;

    util::log::Logger lg;
    boost::system::error_code ec;
    boost::system::error_code ec_read;

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
        : handler_context(h)
        , ws(stream)
        , ibuf(256, allocator_type(h))
        , obuf(256, allocator_type(h))
        , strand(stream.get_io_service())
        , write_phaser(strand)
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
    using composed::bind_handler_context;
    using composed::make_work_guard;

    auto serverToClient = rpc_test_ServerToClient{};
    serverToClient.arg.rpcReply.has_requestId = rpcRequest.has_requestId;
    serverToClient.arg.rpcReply.requestId = rpcRequest.requestId;

    auto visitor = [&serverToClient, this](const auto& req) {
        nanopb::assign(serverToClient.arg.rpcReply.arg, this->request(req));
        nanopb::assign(serverToClient.arg, serverToClient.arg.rpcReply);
    };
    if (nanopb::visit(visitor, rpcRequest.arg)) {
        auto ostream = nanopb::ostream_from_dynamic_buffer(obuf);
        auto success = nanopb::encode(ostream, serverToClient);

        write_phaser.dispatch(bind_handler_context(handler_context, [this, success, n = ostream.bytes_written] {
            if (success) {
                auto output = beast::prepare_buffers(n, obuf.data());
                auto write_handler = bind_handler_context(handler_context,
                        [this, n, work = make_work_guard(write_phaser)](const boost::system::error_code& ec) {
                            obuf.consume(n);
                            BOOST_LOG(lg) << "write_op: " << ec.message();
                        });
                ws.async_write(output, bind_handler_context(handler_context, std::move(write_handler)));
            }
            else {
                obuf.consume(n);
            }
        }));

        if (!success) {
            BOOST_LOG(lg) << "encoding failure";
        }
        else {
            BOOST_LOG(lg) << "received and replied to an RPC request";
        }
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
            while (ibuf.size()) {
                BOOST_LOG(lg) << ibuf.size() << " bytes in buffer";
                auto clientToServer = rpc_test_ClientToServer{};
                auto istream = nanopb::istream_from_dynamic_buffer(ibuf);
                if (!nanopb::decode(istream, clientToServer)) {
                    BOOST_LOG(lg) << "decoding error";
                }
                else {
                    nanopb::visit([this](const auto& x) { this->event(x); }, clientToServer.arg);
                }
            }
            BOOST_LOG(lg) << "reading ...";
            yield return ws.async_read(opcode, ibuf, op(ec_read));
        } while (!ec_read);

        BOOST_LOG(lg) << "read error: " << ec_read.message();

        // To close the connection, we're supposed to call (async_)close, then read until we get
        // an error. First cancel and wait for any outstanding writes, so our async_close doesn't
        // interfere.

        ws.next_layer().cancel(ec_read);  // ignored
        yield return write_phaser.dispatch(op());

        BOOST_LOG(lg) << "closing connection";
        yield return ws.async_close({"Hodor indeed!"}, op(ec));

        do {
            BOOST_LOG(lg) << "reading ...";
            ibuf.consume(ibuf.size());
            yield return ws.async_read(opcode, ibuf, op(ec_read));
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

constexpr composed::operation<server_op<>> async_server;

#include <boost/asio/unyield.hpp>

#endif
