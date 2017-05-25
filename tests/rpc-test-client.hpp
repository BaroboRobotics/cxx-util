#ifndef RPC_TEST_CLIENT_HPP
#define RPC_TEST_CLIENT_HPP

#include <rpc-test.pb.hpp>
#include <pb_asio.hpp>

#include <util/log.hpp>

#include <composed/op.hpp>
#include <composed/async_rpc_read_loop.hpp>
#include <composed/rpc_client.hpp>
#include <composed/phased_stream.hpp>

#include <beast/websocket.hpp>
#include <beast/core/async_completion.hpp>
#include <beast/core/bind_handler.hpp>
#include <beast/core/handler_alloc.hpp>
#include <boost/asio.hpp>

#include <boost/log/attributes/scoped_attribute.hpp>

#include <boost/optional.hpp>

#include <boost/asio/yield.hpp>

// =======================================================================================
// Client operation

template <class Handler = void()>
struct client_op: public boost::asio::coroutine,
        public composed::rpc_client<client_op<Handler>, rpc_test_RpcRequest, rpc_test_RpcReply> {
    using handler_type = Handler;
    using allocator_type = beast::handler_alloc<char, handler_type>;
    using executor_type = composed::handler_executor<handler_type>;

    using logger_type = composed::logger;
    logger_type get_logger() const { return &lg; }

    composed::phaser<executor_type> read_loop_phaser;

    composed::phased_stream<
            executor_type, beast::websocket::stream<boost::asio::ip::tcp::socket&>> stream;

    boost::asio::ip::tcp::endpoint remote_ep;
    typename client_op::client_base_type::transaction rpc;

    mutable util::log::Logger lg;
    boost::system::error_code ec;
    boost::system::error_code reply_ec;

    client_op(handler_type& h, boost::asio::ip::tcp::socket& s,
            boost::asio::ip::tcp::endpoint ep)
        : read_loop_phaser(s.get_io_service(), h)
        , stream(h, s)
        , remote_ep(ep)
        , rpc(*this)
    {
        lg.add_attribute("Role", boost::log::attributes::make_constant("client"));
        lg.add_attribute("TcpRemoteEndpoint", boost::log::attributes::make_constant(remote_ep));
    }

    boost::asio::io_service& get_io_service() { return stream.get_io_service(); }
    // Needed by composed::rpc_client CRTP base.

    void operator()(composed::op<client_op>&);

    // ===================================================================================
    // Event messages

    using typename client_op::client_base_type::event;
    // We need to manually pull our client base's event overload into our class namespace,
    // otherwise our own event overloads will hide it.

    template <class H>
    void event(const rpc_test_Quux&, H&& handler);

    template <class T, class Token>
    auto async_write_event(const T& message, Token&& token) {
        // Needed by composed::rpc_client CRTP base.
        auto clientToServer = rpc_test_ClientToServer{};
        nanopb::assign(clientToServer.arg, message);
        return stream.async_write(clientToServer, std::forward<Token>(token));
    }
};

template <class Handler>
void client_op<Handler>::operator()(composed::op<client_op>& op) {
    if (!ec) reenter(this) {
        yield return stream.next_layer().next_layer().async_connect(remote_ep, op(ec));
        yield return stream.next_layer().async_handshake(
                "hodorhodorhodor.com", "/cgi-bin/hax", op(ec));

        BOOST_LOG(lg) << "WebSocket connected";

        stream.next_layer().set_option(
                beast::websocket::message_type{beast::websocket::opcode::binary});

        {
            auto cleanup = op.wrap(
                    [this, work = make_work_guard(read_loop_phaser)](const boost::system::error_code& ec) {
                        if (ec == beast::websocket::error::closed) {
                            BOOST_LOG(lg) << "WebSocket closed, remote code/reason: "
                                    << stream.next_layer().reason().code << '/'
                                    << stream.next_layer().reason().reason.c_str();
                        }
                        else {
                            BOOST_LOG(lg) << "read loop error: " << ec.message();
                        }
                    });
            composed::async_rpc_read_loop<rpc_test_ServerToClient>(
                    stream.next_layer(), *this, std::move(cleanup));
        }

        yield return async_write_event(rpc_test_Quux{}, op(ec));
        BOOST_LOG(lg) << "sent a Quux";

        using namespace std::literals;
        yield return rpc.async_do_request(
                rpc_test_SetProperty_In{true, 333.0},
                rpc_test_RpcReply_setProperty_tag,
                1s,
                op(reply_ec));
        if (!reply_ec) {
            BOOST_LOG(lg) << "SetProperty reply";
        }
        else {
            BOOST_LOG(lg) << "SetProperty reply error: " << reply_ec.message();
        }

        rpc.reset();

        yield return rpc.async_do_request(
                rpc_test_GetProperty_In{},
                rpc_test_RpcReply_getProperty_tag,
                1s,
                op(reply_ec));
        if (!reply_ec) {
            BOOST_LOG(lg) << "GetProperty reply: " << rpc.reply().arg.getProperty.value;
        }
        else {
            BOOST_LOG(lg) << "GetProperty reply error: " << reply_ec.message();
        }

        BOOST_LOG(lg) << "closing connection";
        yield return stream.next_layer().async_close({"Hodor!"}, op(std::ignore));

        BOOST_LOG(lg) << "waiting for read loop";
        yield return read_loop_phaser.dispatch(op());
        BOOST_LOG(lg) << "read loop done, client op exiting";
        yield return read_loop_phaser.get_io_service().post(op());
    }
    else {
        BOOST_LOG(lg) << "error: " << ec.message();
    }
    op.complete();
}

constexpr composed::operation<client_op<>> async_client;

template <class Handler>
template <class H>
void client_op<Handler>::event(const rpc_test_Quux& quux, H&& handler) {
    BOOST_LOG(lg) << "Received a Quux";
    stream.get_io_service().post(std::forward<H>(handler));
}

#include  <boost/asio/unyield.hpp>

#endif
