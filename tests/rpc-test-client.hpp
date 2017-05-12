#ifndef RPC_TEST_CLIENT_HPP
#define RPC_TEST_CLIENT_HPP

#include <rpc-test.pb.hpp>
#include <pb_asio.hpp>

#include <util/log.hpp>

#include <composed/op.hpp>
#include <composed/async_rpc_read_loop.hpp>
#include <composed/rpc_multiplexer.hpp>

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
struct client_op: boost::asio::coroutine {
    using handler_type = Handler;
    using allocator_type = beast::handler_alloc<char, handler_type>;
    using executor_type = composed::handler_executor<handler_type>;

    using logger_type = composed::logger;
    logger_type get_logger() const { return &lg; }

    beast::websocket::stream<boost::asio::ip::tcp::socket>& ws;
    composed::phaser<executor_type> read_loop_phaser;
    composed::phaser<executor_type> write_phaser;

    boost::asio::ip::tcp::endpoint remote_ep;

    composed::rpc_multiplexer<rpc_test_RpcReply> muxer;
    typename decltype(muxer)::transaction transaction;

    mutable util::log::Logger lg;
    boost::system::error_code ec;
    boost::system::error_code reply_ec;

    client_op(handler_type& h, beast::websocket::stream<boost::asio::ip::tcp::socket>& stream,
            boost::asio::ip::tcp::endpoint ep)
        : ws(stream)
        , read_loop_phaser(stream.get_io_service(), h)
        , write_phaser(stream.get_io_service(), h)
        , remote_ep(ep)
        , transaction(stream.get_io_service(), muxer)
    {
        lg.add_attribute("Role", boost::log::attributes::make_constant("client"));
        lg.add_attribute("TcpRemoteEndpoint", boost::log::attributes::make_constant(remote_ep));
    }

    void operator()(composed::op<client_op>&);

    // ===================================================================================
    // Event messages

    template <class H>
    void event(const rpc_test_RpcReply& rpcReply, H&& handler);
    template <class H>
    void event(const rpc_test_Quux&, H&& handler);

private:
    template <class H = void(boost::system::error_code)>
    struct write_event_op;

    template <class T, class Token>
    auto async_write_event(const T& message, Token&& token) {
        return composed::operation<write_event_op<>>{}(*this, message, std::forward<Token>(token));
    }

    template <class T, class Token>
    auto async_write_request(uint32_t id, const T& message, Token&& token) {
        rpc_test_RpcRequest request;
        request.has_requestId = true;
        request.requestId = id;
        nanopb::assign(request.arg, message);
        return async_write_event(request, std::forward<Token>(token));
    }

    template <class T, class H = void(boost::system::error_code)>
    struct do_request_op;

    template <class T, class Duration, class Token>
    auto async_do_request(
            const T& message, pb_size_t expected_tag, Duration&& duration, Token&& token) {
        return composed::operation<do_request_op<T>>{}(*this, message, expected_tag,
                std::forward<Duration>(duration), std::forward<Token>(token));
    }
};

template <class Handler>
void client_op<Handler>::operator()(composed::op<client_op>& op) {
    if (!ec) reenter(this) {
        yield return ws.next_layer().async_connect(remote_ep, op(ec));
        yield return ws.async_handshake("hodorhodorhodor.com", "/cgi-bin/hax", op(ec));

        BOOST_LOG(lg) << "WebSocket connected";

        ws.set_option(beast::websocket::message_type{beast::websocket::opcode::binary});

        {
            auto cleanup = op.wrap(
                    [this, work = make_work_guard(read_loop_phaser)](const boost::system::error_code& ec) {
                        if (ec == beast::websocket::error::closed) {
                            BOOST_LOG(lg) << "WebSocket closed, remote code/reason: "
                                    << ws.reason().code << '/' << ws.reason().reason.c_str();
                        }
                        else {
                            BOOST_LOG(lg) << "read loop error: " << ec.message();
                        }
                    });
            composed::async_rpc_read_loop<rpc_test_ServerToClient>(ws, *this, std::move(cleanup));
        }

        yield return async_write_event(rpc_test_Quux{}, op(ec));
        BOOST_LOG(lg) << "sent a Quux";

        using namespace std::literals;
        yield return async_do_request(
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

        yield return async_do_request(
                rpc_test_GetProperty_In{},
                rpc_test_RpcReply_getProperty_tag,
                100ms,
                op(reply_ec));
        if (!reply_ec) {
            BOOST_LOG(lg) << "GetProperty reply: " << transaction.future().value().arg.getProperty.value;
        }
        else {
            BOOST_LOG(lg) << "GetProperty reply error: " << reply_ec.message();
        }

        BOOST_LOG(lg) << "closing connection";
        yield return ws.async_close({"Hodor!"}, op(std::ignore));

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
void client_op<Handler>::event(const rpc_test_RpcReply& rpcReply, H&& handler) {
    if (!rpcReply.has_requestId) {
        BOOST_LOG(lg) << "Received an RPC reply without a request ID";
        ws.get_io_service().post(std::forward<H>(handler));
    }
    else {
        BOOST_LOG_SCOPED_LOGGER_TAG(lg, "RequestId", std::to_string(rpcReply.requestId));
        BOOST_LOG(lg) << "Fulfilling request";
        muxer.fulfill(rpcReply.requestId, rpcReply);
        ws.get_io_service().post(std::forward<H>(handler));
    }
}

template <class Handler>
template <class H>
void client_op<Handler>::event(const rpc_test_Quux& quux, H&& handler) {
    BOOST_LOG(lg) << "Received a Quux";
    ws.get_io_service().post(std::forward<H>(handler));
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
        yield return self.ws.async_write(buf.data(), op(ec));
    }
    else {
        BOOST_LOG(lg) << "write_event_op error: " << ec.message();
    }
    op.complete(ec);
}

template <class Handler>
template <class T, class H>
struct client_op<Handler>::do_request_op: boost::asio::coroutine {
    using handler_type = H;
    using allocator_type = beast::handler_alloc<char, handler_type>;

    using logger_type = composed::logger;
    logger_type get_logger() const { return &lg; }

    client_op& self;

    const T& message;
    // May dangle after initiation! Only use this to pass to the first async_* call.

    pb_size_t expected_tag;
    std::chrono::nanoseconds duration;

    mutable util::log::Logger lg;
    boost::system::error_code ec;

    template <class Duration>
    do_request_op(handler_type& h, client_op& s, const T& m, pb_size_t t, Duration&& d)
        : self(s)
        , message(m)
        , expected_tag(t)
        , duration(std::forward<Duration>(d))
        , lg(composed::get_associated_logger(h).clone())
    {
        lg.add_attribute("RequestId",
                boost::log::attributes::make_constant(std::to_string(self.transaction.id())));
    }

    void operator()(composed::op<do_request_op>& op);
};

template <class Handler>
template <class T, class H>
void client_op<Handler>::do_request_op<T, H>::operator()(composed::op<do_request_op>& op) {
    if (!ec) reenter(this) {
        yield return self.async_write_request(self.transaction.id(), message, op(ec));

        BOOST_LOG(lg) << "sent " << boost::typeindex::type_id<T>().pretty_name()
                << " request, awaiting reply";

        yield return self.transaction.async_wait_for(expected_tag, duration, op(ec));
        self.transaction.reset();
    }
    op.complete(ec);
}

#include  <boost/asio/unyield.hpp>

#endif
