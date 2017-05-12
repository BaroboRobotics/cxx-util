#ifndef RPC_TEST_CLIENT_HPP
#define RPC_TEST_CLIENT_HPP

#include <rpc-test.pb.hpp>
#include <pb_asio.hpp>

#include <util/log.hpp>

#include <composed/op.hpp>
#include <composed/async_rpc_read_loop.hpp>
#include <composed/rpc_client.hpp>

#include <beast/websocket.hpp>
#include <beast/core/async_completion.hpp>
#include <beast/core/bind_handler.hpp>
#include <beast/core/handler_alloc.hpp>
#include <boost/asio.hpp>

#include <boost/log/attributes/scoped_attribute.hpp>

#include <boost/optional.hpp>
#include <boost/type_index.hpp>

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

    beast::websocket::stream<boost::asio::ip::tcp::socket>& stream;
    composed::phaser<executor_type> write_phaser;
    composed::phaser<executor_type> read_loop_phaser;

    boost::asio::ip::tcp::endpoint remote_ep;
    typename client_op::client_base_type::transaction rpc;

    mutable util::log::Logger lg;
    boost::system::error_code ec;
    boost::system::error_code reply_ec;

    client_op(handler_type& h, beast::websocket::stream<boost::asio::ip::tcp::socket>& s,
            boost::asio::ip::tcp::endpoint ep)
        : stream(s)
        , write_phaser(s.get_io_service(), h)
        , read_loop_phaser(s.get_io_service(), h)
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
    template <class H>
    void event(const rpc_test_Quux&, H&& handler);

    template <class H = void(boost::system::error_code)>
    struct write_event_op;

    template <class T, class Token>
    auto async_write_event(const T& message, Token&& token) {
        // Needed by composed::rpc_client CRTP base.
        return composed::operation<write_event_op<>>{}(*this, message, std::forward<Token>(token));
    }
};

template <class Handler>
void client_op<Handler>::operator()(composed::op<client_op>& op) {
    if (!ec) reenter(this) {
        yield return stream.next_layer().async_connect(remote_ep, op(ec));
        yield return stream.async_handshake("hodorhodorhodor.com", "/cgi-bin/hax", op(ec));

        BOOST_LOG(lg) << "WebSocket connected";

        stream.set_option(beast::websocket::message_type{beast::websocket::opcode::binary});

        {
            auto cleanup = op.wrap(
                    [this, work = make_work_guard(read_loop_phaser)](const boost::system::error_code& ec) {
                        if (ec == beast::websocket::error::closed) {
                            BOOST_LOG(lg) << "WebSocket closed, remote code/reason: "
                                    << stream.reason().code << '/' << stream.reason().reason.c_str();
                        }
                        else {
                            BOOST_LOG(lg) << "read loop error: " << ec.message();
                        }
                    });
            composed::async_rpc_read_loop<rpc_test_ServerToClient>(stream, *this, std::move(cleanup));
        }

        yield return async_write_event(rpc_test_Quux{}, op(ec));
        BOOST_LOG(lg) << "sent a Quux";

        using namespace std::literals;
        yield return rpc.async_do_request(
                rpc_test_SetProperty_In{true, 333.0},
                rpc_test_RpcReply_setProperty_tag,
                100ms,
                op(reply_ec));
        if (!reply_ec) {
            BOOST_LOG(lg) << "SetProperty reply";
        }
        else {
            BOOST_LOG(lg) << "GetProperty reply error: " << reply_ec.message();
        }

        rpc.reset();

        yield return rpc.async_do_request(
                rpc_test_GetProperty_In{},
                rpc_test_RpcReply_getProperty_tag,
                100ms,
                op(reply_ec));
        if (!reply_ec) {
            BOOST_LOG(lg) << "GetProperty reply: " << rpc.reply().arg.getProperty.value;
        }
        else {
            BOOST_LOG(lg) << "GetProperty reply error: " << reply_ec.message();
        }

        BOOST_LOG(lg) << "closing connection";
        yield return stream.async_close({"Hodor!"}, op(std::ignore));

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

template <class Handler>
template <class H>
struct client_op<Handler>::write_event_op: boost::asio::coroutine {
    using handler_type = H;
    using allocator_type = beast::handler_alloc<char, handler_type>;

    client_op& self;
    composed::work_guard<composed::phaser<client_op::executor_type>> work;

    beast::basic_streambuf<allocator_type> buf;

    composed::associated_logger_t<handler_type> lg;
    boost::system::error_code ec;

    template <class T>
    write_event_op(handler_type& h, client_op& s, const T& message)
        : self(s)
        , buf(256, allocator_type(h))
        , lg(composed::get_associated_logger(h))
    {
        auto clientToServer = rpc_test_ClientToServer{};
        nanopb::assign(clientToServer.arg, message);
        auto success = nanopb::encode(buf, clientToServer);
        BOOST_ASSERT(success);
    }

    void operator()(composed::op<write_event_op>& op);
};

template <class Handler>
template <class H>
void client_op<Handler>::write_event_op<H>::
operator()(composed::op<write_event_op>& op) {
    if (!ec) reenter(this) {
        yield return self.write_phaser.dispatch(op());
        work = composed::make_work_guard(self.write_phaser);
        yield return self.stream.async_write(buf.data(), op(ec));
    }
    else {
        BOOST_LOG(lg) << "write_event_op error: " << ec.message();
    }
    op.complete(ec);
}

#include  <boost/asio/unyield.hpp>

#endif
