#include "./rpc-test-server.hpp"
#include "./rpc-test-client.hpp"

#include <util/overload.hpp>
#include <util/programpath.hpp>

//#include <util/doctest.h>

#include <composed/op_logger.hpp>
#include <composed/phaser.hpp>

#include <beast/websocket.hpp>
#include <beast/core/handler_alloc.hpp>
#include <boost/asio.hpp>

#include <boost/program_options/parsers.hpp>

#include <iostream>
#include <memory>

#include <boost/asio/yield.hpp>

// =======================================================================================
// Server

template <class Handler = void()>
struct server_op {
    using handler_type = Handler;
    using allocator_type = beast::handler_alloc<char, handler_type>;
    using logger_type = composed::logger;
    logger_type get_logger() const { return &lg; }

    TestServer server;
    boost::asio::ip::tcp::acceptor acceptor;
    beast::websocket::stream<boost::asio::ip::tcp::socket>& ws;
    boost::asio::ip::tcp::endpoint serverEndpoint;
    boost::asio::ip::tcp::endpoint clientEndpoint;
    beast::websocket::opcode opcode;
    beast::streambuf buf;

    util::log::Logger lg;
    boost::asio::coroutine coro;
    boost::system::error_code ec;
    boost::system::error_code ecRead;

    server_op(handler_type& h, beast::websocket::stream<boost::asio::ip::tcp::socket>& stream,
            boost::asio::ip::tcp::endpoint ep)
        : acceptor(stream.get_io_service(), ep)
        , ws(stream)
        , serverEndpoint(ep)
    {
        lg.add_attribute("Role", boost::log::attributes::constant<std::string>("server"));
    }

    void operator()(composed::op<server_op>&);
};

template <class Handler>
void server_op<Handler>::operator()(composed::op<server_op>& op) {
    if (!ec) reenter (coro) {
        yield acceptor.async_accept(ws.next_layer(), clientEndpoint, op(ec));
        yield ws.async_accept(op(ec));

        BOOST_LOG(lg) << "accepted WebSocket connection from " << clientEndpoint;

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
                    nanopb::visit(server, clientToServer.arg);
                }
            }
            BOOST_LOG(lg) << "reading ...";
            yield ws.async_read(opcode, buf, op(ecRead));
        } while (!ecRead);

        BOOST_LOG(lg) << "read error: " << ecRead.message();

        // To close the connection, we're supposed to call (async_)close, then read until we get
        // an error.
        BOOST_LOG(lg) << "closing connection";
        yield ws.async_close({"Hodor indeed!"}, op(ec));

        do {
            BOOST_LOG(lg) << "reading ...";
            buf.consume(buf.size());
            yield ws.async_read(opcode, buf, op(ecRead));
        } while (!ecRead);

        if (ecRead == beast::websocket::error::closed) {
            BOOST_LOG(lg) << "WebSocket closed, remote gave code/reason: "
                    << ws.reason().code << '/' << ws.reason().reason.c_str();
        }
        else {
            BOOST_LOG(lg) << "read error: " << ecRead.message();
        }

        op.complete();
    }
    else {
        BOOST_LOG(lg) << "error: " << ec.message();
        op.complete();
    }
}

// =======================================================================================
// Client

template <class Handler = void()>
struct client_op {
    using handler_type = Handler;
    using allocator_type = beast::handler_alloc<char, handler_type>;
    using logger_type = composed::logger;
    logger_type get_logger() const { return &lg; }

    TestClient client;
    beast::websocket::stream<boost::asio::ip::tcp::socket>& ws;
    boost::asio::ip::tcp::endpoint serverEndpoint;
    beast::websocket::opcode opcode;
    beast::streambuf buf;

    util::log::Logger lg;
    boost::asio::coroutine coro;
    boost::system::error_code ec;
    boost::system::error_code ecRead;

    client_op(handler_type&, beast::websocket::stream<boost::asio::ip::tcp::socket>& stream,
            boost::asio::ip::tcp::endpoint ep)
        : ws(stream)
        , serverEndpoint(ep)
    {
        lg.add_attribute("Role", boost::log::attributes::constant<std::string>("client"));
    }

    void operator()(composed::op<client_op>&);
};

template <class Handler>
void client_op<Handler>::operator()(composed::op<client_op>& op) {
    if (!ec) reenter (coro) {
        yield ws.next_layer().async_connect(serverEndpoint, op(ec));
        yield ws.async_handshake("hodorhodorhodor.com", "/cgi-bin/hax", op(ec));

        BOOST_LOG(lg) << "established WebSocket connection to " << serverEndpoint;

        ws.set_option(beast::websocket::message_type{beast::websocket::opcode::binary});

        yield {
            auto clientToServer = rpc_test_ClientToServer{};
            clientToServer.arg.quux.value = 666;
            nanopb::assign(clientToServer.arg, clientToServer.arg.quux);
            auto ostream = nanopb::ostream_from_dynamic_buffer(buf);
            if (!nanopb::encode(ostream, clientToServer)) {
                BOOST_LOG(lg) << "encoding failure";
                buf.consume(buf.size());
            }
            BOOST_LOG(lg) << "Encoded " << ostream.bytes_written;
            ws.async_write(buf.data(), op(ec));
        }
        BOOST_LOG(lg) << "wrote " << buf.size() << " bytes";
        buf.consume(buf.size());

        // To close the connection, we're supposed to call (async_)close, then read until we get
        // an error.

        BOOST_LOG(lg) << "closing connection";
        yield ws.async_close({"Hodor!"}, op(ec));

        do {
            BOOST_LOG(lg) << "reading ...";
            buf.consume(buf.size());
            yield ws.async_read(opcode, buf, op(ecRead));
        } while (!ecRead);

        if (ecRead == beast::websocket::error::closed) {
            BOOST_LOG(lg) << "WebSocket closed, remote gave code/reason: "
                    << ws.reason().code << '/' << ws.reason().reason.c_str();
        }
        else {
            BOOST_LOG(lg) << "read error: " << ecRead.message();
        }

        op.complete();
    }
    else {
        BOOST_LOG(lg) << "error: " << ec.message();
        op.complete();
    }
}



// =======================================================================================
// Main

struct MainHandler {
    using logger_type = composed::logger;
    logger_type lg;
    logger_type get_logger() const { return lg; }

    explicit MainHandler(util::log::Logger& l): lg(&l) {}

    size_t allocations = 0;
    size_t deallocations = 0;

    void operator()() {
        BOOST_LOG(lg) << "main task completed, " << allocations << '/' << deallocations
                << " allocations/deallocations";
    }

    friend void* asio_handler_allocate(size_t size, MainHandler* self) {
        ++self->allocations;

        // Forward to the default implementation of asio_handler_allocate.
        int dummy;
        return beast_asio_helpers::allocate(size, dummy);
    }

    friend void asio_handler_deallocate(void* pointer, size_t size, MainHandler* self) {
        ++self->deallocations;

        // Forward to the default implementation of asio_handler_deallocate.
        int dummy;
        beast_asio_helpers::deallocate(pointer, size, dummy);
    }

    template <class Function>
    friend void asio_handler_invoke(Function&& function, MainHandler* self) {
        // Forward to the default implementation of asio_handler_invoke.
        int dummy;
        beast_asio_helpers::invoke(std::forward<Function>(function), dummy);
    }

    friend bool asio_handler_is_continuation(MainHandler* self) {
        return true;
    }
};



template <class Handler = void()>
struct main_op {
    using handler_type = Handler;
    using allocator_type = beast::handler_alloc<char, handler_type>;

    boost::asio::signal_set& trap;
    boost::asio::ip::tcp::endpoint serverEndpoint;

    composed::phaser<handler_type> phaser;

    beast::websocket::stream<boost::asio::ip::tcp::socket> serverStream;
    beast::websocket::stream<boost::asio::ip::tcp::socket> clientStream;

    composed::associated_logger_t<handler_type> lg;
    boost::asio::coroutine coro;
    boost::system::error_code ec;
    int sigNo;

    main_op(handler_type& h, boost::asio::signal_set& ss, boost::asio::ip::tcp::endpoint ep)
        : trap(ss)
        , serverEndpoint(ep)
        , phaser(ss.get_io_service(), h)
        , serverStream(ss.get_io_service())
        , clientStream(ss.get_io_service())
        , lg(composed::get_associated_logger(h))
    {
        lg.add_attribute("Role", boost::log::attributes::constant<std::string>("main"));
    }

    void operator()(composed::op<main_op>&);
};

template <class Handler>
void main_op<Handler>::operator()(composed::op<main_op>& op) {
    reenter (coro) {
        yield {
            auto next = op(ec, sigNo);
            composed::async_run<server_op<>>(serverStream, serverEndpoint, phaser.discard(next));
            composed::async_run<client_op<>>(clientStream, serverEndpoint, phaser.discard(next));
            trap.async_wait(next);
        }

        if (!ec) {
            BOOST_LOG(lg) << "Trap caught signal " << sigNo;
        }
        else {
            BOOST_LOG(lg) << "Trap error: " << ec.message();
        }

        serverStream.next_layer().close();
        clientStream.next_layer().close();

        yield phaser.async_wait(op(ec));

        op.complete();
    }
};




namespace po = boost::program_options;

int main (int argc, char** argv) {
    std::string serverHost;
    uint16_t serverPort;

    auto optsDesc = po::options_description{util::programPath().string() + " command line options"};
    optsDesc.add_options()
        ("help", "display this text")
        ("version", "display version")
        ("host", po::value<std::string>(&serverHost)
            ->value_name("<host>")
            ->default_value("0.0.0.0"),
            "bind to this host")
        ("port", po::value<uint16_t>(&serverPort)
            ->value_name("<port>")
            ->default_value(17739),
            "bind to this port/service")
    ;

    optsDesc.add(util::log::optionsDescription());

    auto options = boost::program_options::variables_map{};
    po::store(po::parse_command_line(argc, argv, optsDesc), options);
    po::notify(options);

    if (options.count("help")) {
        std::cout << optsDesc << '\n';
        return 0;
    }

    if (options.count("version")) {
        std::cout << util::programPath().string() << " (no version)\n";
        return 0;
    }

    util::log::Logger lg;
    boost::asio::io_service context;
    const auto serverEndpoint = boost::asio::ip::tcp::endpoint{
            boost::asio::ip::address::from_string(serverHost), serverPort};
    boost::asio::signal_set trap{context, SIGINT, SIGTERM};

    composed::async_run<main_op<>>(trap, serverEndpoint, MainHandler{lg});

    auto nHandlers = context.run();
    BOOST_LOG(lg) << "ran " << nHandlers << " handlers, exiting";
}

#include <boost/asio/unyield.hpp>