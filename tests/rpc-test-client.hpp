#ifndef RPC_TEST_CLIENT_HPP
#define RPC_TEST_CLIENT_HPP

#include <rpc-test.pb.hpp>
#include <pb_asio.hpp>

#include <util/log.hpp>
#include <util/overload.hpp>

#include <composed/op.hpp>
#include <composed/rpc_client.hpp>
#include <composed/rpc_stream.hpp>

#include <beast/websocket.hpp>
#include <beast/core/bind_handler.hpp>
#include <beast/core/handler_alloc.hpp>
#include <boost/asio.hpp>

#include <boost/log/attributes/scoped_attribute.hpp>

#include <boost/optional.hpp>

#include <boost/asio/yield.hpp>

// =======================================================================================
// Client operation

template <class Handler = void()>
struct client_op: boost::asio::coroutine {
    using handler_type = Handler;
    using allocator_type = beast::handler_alloc<char, handler_type>;
    using executor_type = composed::handler_executor<handler_type>;

    using logger_type = composed::logger;
    logger_type get_logger() const { return &lg; }

    beast::websocket::stream<boost::asio::ip::tcp::socket&> stream;
    composed::websocket msgStream;
    composed::rpc_stream<composed::websocket&,
            rpc_test_ClientToServer, rpc_test_ServerToClient> rpcStream;
    composed::rpc_client<decltype(rpcStream)&, rpc_test_RpcRequest, rpc_test_RpcReply> rpcClient;
    typename decltype(rpcClient)::transactor rpc;

    boost::asio::ip::tcp::endpoint remote_ep;

    mutable util::log::Logger lg;
    boost::system::error_code ec;
    boost::system::error_code reply_ec;

    client_op(handler_type& h, boost::asio::ip::tcp::socket& s,
            boost::asio::ip::tcp::endpoint ep)
        : stream(s)
        , msgStream(stream)
        , rpcStream(msgStream)
        , rpcClient(rpcStream)
        , rpc(rpcClient)
        , remote_ep(ep)
    {
        lg.add_attribute("Role", boost::log::attributes::make_constant("client"));
        lg.add_attribute("TcpRemoteEndpoint", boost::log::attributes::make_constant(remote_ep));
    }

    void operator()(composed::op<client_op>&);

    // ===================================================================================
    // Event messages

    void event(const rpc_test_Quux&);
};

template <class Handler>
void client_op<Handler>::operator()(composed::op<client_op>& op) {
    if (!ec) reenter(this) {
        yield return stream.next_layer().async_connect(remote_ep, op(ec));
        yield return stream.async_handshake(
                "hodorhodorhodor.com", "/cgi-bin/hax", op(ec));

        BOOST_LOG(lg) << "WebSocket connected";

        stream.binary(true);

        rpcStream.async_run_read_loop(
                util::overload(
                        [this](const auto& e, auto&& h) {
                            this->event(e);
                            stream.get_io_service().post(std::forward<decltype(h)>(h));
                        },
                        [this](const rpc_test_RpcReply& e, auto&& h) {
                            rpcClient.event(e);
                            stream.get_io_service().post(std::forward<decltype(h)>(h));
                        }),
                op.wrap([this](const boost::system::error_code& ec) {
                    if (ec == beast::websocket::error::closed) {
                        BOOST_LOG(lg) << "read loop stopped because remote closed WebSocket: "
                                << stream.reason().code << '/'
                                << stream.reason().reason.c_str();
                    }
                    else {
                        BOOST_LOG(lg) << "read loop error: " << ec.message();
                    }
                }));

        yield return rpcStream.async_write(rpc_test_Quux{}, op(ec));
        BOOST_LOG(lg) << "sent a Quux";

        using namespace std::literals;
        yield return rpc.async_do_request(
                rpc_test_setProperty_In{true, 333.0},
                rpc_test_RpcReply_setProperty_tag,
                1s,
                op(reply_ec, std::ignore));
        if (!reply_ec) {
            BOOST_LOG(lg) << "SetProperty reply";
        }
        else {
            BOOST_LOG(lg) << "SetProperty reply error: " << reply_ec.message();
        }

        yield return rpc.async_do_request(
                rpc_test_getProperty_In{},
                rpc_test_RpcReply_getProperty_tag,
                1s,
                op(reply_ec, std::ignore));
        if (!reply_ec) {
            BOOST_LOG(lg) << "GetProperty reply: " << rpc.reply().getProperty.value;
        }
        else {
            BOOST_LOG(lg) << "GetProperty reply error: " << reply_ec.message();
        }

        BOOST_LOG(lg) << "closing connection";
        yield return stream.async_close({"Hodor!"}, op(std::ignore));

        BOOST_LOG(lg) << "waiting for read loop";
        yield return rpcStream.async_stop_read_loop(op(ec));
        BOOST_LOG(lg) << "read loop done, client op exiting";
    }
    else {
        BOOST_LOG(lg) << "error: " << ec.message();
    }
    op.complete();
}

constexpr composed::operation<client_op<>> async_client;

template <class Handler>
void client_op<Handler>::event(const rpc_test_Quux& quux) {
    BOOST_LOG(lg) << "Received a Quux";
}

#include  <boost/asio/unyield.hpp>

#endif
