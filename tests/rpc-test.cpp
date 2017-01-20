#include "./rpc-test-server.hpp"
#include "./rpc-test-client.hpp"

#include <util/asio/operation.hpp>
#include <util/log.hpp>
#include <util/overload.hpp>
#include <util/programpath.hpp>

//#include <util/doctest.h>

#include <beast/websocket.hpp>
#include <boost/asio.hpp>

#include <boost/program_options/parsers.hpp>

#include <iostream>
#include <memory>

#include <boost/asio/yield.hpp>

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

    auto serverTask =
    [ server = TestServer{}
    , acceptor = boost::asio::ip::tcp::acceptor{context, serverEndpoint}
    , ws = beast::websocket::stream<boost::asio::ip::tcp::socket>{context}
    , terminator = std::make_unique<boost::asio::signal_set>(context, SIGINT, SIGTERM)
    , clientEndpoint = boost::asio::ip::tcp::endpoint{}
    , opcode = beast::websocket::opcode{}
    , buf = beast::streambuf{}
    ](auto&& op, boost::system::error_code ec = {}, int sigNo = 0) mutable {
        reenter (op) {
            fork op.runChild();
            if (op.is_child()) {
                yield terminator->async_wait(std::move(op));
                if (!ec) {
                    BOOST_LOG(op.log()) << "Closing after signal " << sigNo;
                    acceptor.close(ec);
                    BOOST_LOG(op.log()) << "acceptor closed: " << ec.message();
                    ws.next_layer().close(ec);
                    // TODO: more correct way of closing?
                    BOOST_LOG(op.log()) << "WebSocket closed: " << ec.message();
                }
                return;
            }

            yield acceptor.async_accept(ws.next_layer(), clientEndpoint, std::move(op));
            if (ec) { BOOST_LOG(op.log()) << ec.message(); op.complete(ec); return; }

            BOOST_LOG(op.log()) << "accepted TCP connection from " << clientEndpoint;

            yield ws.async_accept(std::move(op));
            if (ec) { BOOST_LOG(op.log()) << ec.message(); op.complete(ec); return; }

            BOOST_LOG(op.log()) << "accepted WebSocket connection from " << clientEndpoint;

            ws.set_option(beast::websocket::message_type{beast::websocket::opcode::binary});

            do {
                while (buf.size()) {
                    BOOST_LOG(op.log()) << buf.size() << " bytes in buffer";
                    auto clientToServer = rpc_test_ClientToServer{};
                    auto istream = nanopb::istream_from_dynamic_buffer(buf);
                    if (!nanopb::decode(istream, clientToServer)) {
                        BOOST_LOG(op.log()) << "decoding error";
                    }
                    else {
                        nanopb::visit(server, clientToServer.arg);
                    }
                }
                BOOST_LOG(op.log()) << "reading ...";
                yield ws.async_read(opcode, buf, std::move(op));
            } while (!ec);

            BOOST_LOG(op.log()) << "read error: " << ec.message();

            // To close the connection, we're supposed to call (async_)close, then read until we get
            // an error.
            BOOST_LOG(op.log()) << "closing connection";
            yield ws.async_close({"Hodor indeed!"}, std::move(op));
            if (ec) { BOOST_LOG(op.log()) << ec.message(); op.complete(ec); return; }

            do {
                BOOST_LOG(op.log()) << "reading ...";
                buf.consume(buf.size());
                yield ws.async_read(opcode, buf, std::move(op));
            } while (!ec);

            if (ec == beast::websocket::error::closed) {
                BOOST_LOG(op.log()) << "WebSocket closed, remote gave code/reason: "
                        << ws.reason().code << '/' << ws.reason().reason.c_str();
            }
            else {
                BOOST_LOG(op.log()) << "read error: " << ec.message();
            }

            terminator->cancel();
            op.complete(ec);
        }
    };

    auto clientTask =
    [ client = TestClient{}
    , serverEndpoint
    , ws = beast::websocket::stream<boost::asio::ip::tcp::socket>(context)
    , terminator = std::make_unique<boost::asio::signal_set>(context, SIGINT, SIGTERM)
    , opcode = beast::websocket::opcode{}
    , buf = beast::streambuf{}
    ](auto&& op, boost::system::error_code ec = {}, int sigNo = 0) mutable {
        reenter (op) {
            fork op.runChild();
            if (op.is_child()) {
                yield terminator->async_wait(std::move(op));
                if (!ec) {
                    BOOST_LOG(op.log()) << "Closing after signal " << sigNo;
                    ws.next_layer().close(ec);
                    // TODO: more correct way of closing?
                    BOOST_LOG(op.log()) << "WebSocket closed: " << ec.message();
                }
                return;
            }

            yield ws.next_layer().async_connect(serverEndpoint, std::move(op));
            if (ec) { BOOST_LOG(op.log()) << ec.message(); op.complete(ec); return; }

            BOOST_LOG(op.log()) << "established TCP connection to " << serverEndpoint;

            yield ws.async_handshake("hodorhodorhodor.com", "/cgi-bin/hax", std::move(op));
            if (ec) { BOOST_LOG(op.log()) << ec.message(); op.complete(ec); return; }

            BOOST_LOG(op.log()) << "established WebSocket connection to " << serverEndpoint;

            ws.set_option(beast::websocket::message_type{beast::websocket::opcode::binary});

            yield {
                auto clientToServer = rpc_test_ClientToServer{};
                clientToServer.arg.quux.value = 666;
                nanopb::assign(clientToServer.arg, clientToServer.arg.quux);
                auto ostream = nanopb::ostream_from_dynamic_buffer(buf);
                if (!nanopb::encode(ostream, clientToServer)) {
                    BOOST_LOG(op.log()) << "encoding failure";
                    buf.consume(buf.size());
                }
                BOOST_LOG(op.log()) << "Encoded " << ostream.bytes_written;
                ws.async_write(buf.data(), std::move(op));
            }
            BOOST_LOG(op.log()) << "wrote " << buf.size() << " bytes";
            buf.consume(buf.size());

            // To close the connection, we're supposed to call (async_)close, then read until we get
            // an error.

            BOOST_LOG(op.log()) << "closing connection";
            yield ws.async_close({"Hodor!"}, std::move(op));
            if (ec) { BOOST_LOG(op.log()) << ec.message(); op.complete(ec); return; }

            do {
                BOOST_LOG(op.log()) << "reading ...";
                buf.consume(buf.size());
                yield ws.async_read(opcode, buf, std::move(op));
            } while (!ec);

            if (ec == beast::websocket::error::closed) {
                BOOST_LOG(op.log()) << "WebSocket closed, remote gave code/reason: "
                        << ws.reason().code << '/' << ws.reason().reason.c_str();
            }
            else {
                BOOST_LOG(op.log()) << "read error: " << ec.message();
            }

            terminator->cancel();
            op.complete(ec);
        }
    };

    auto dummy = [](boost::system::error_code) {};

    auto serverHandler = util::asio::addAssociatedLogger(dummy, util::log::Logger{});
    serverHandler.log().add_attribute(
            "Role", boost::log::attributes::constant<std::string>("server"));

    auto clientHandler = util::asio::addAssociatedLogger(dummy, util::log::Logger{});
    clientHandler.log().add_attribute(
            "Role", boost::log::attributes::constant<std::string>("client"));

    util::asio::asyncDispatch(
        context,
        std::make_tuple(make_error_code(boost::asio::error::operation_aborted)),
        std::move(serverTask),
        std::move(serverHandler)
    );

    util::asio::asyncDispatch(
        context,
        std::make_tuple(make_error_code(boost::asio::error::operation_aborted)),
        std::move(clientTask),
        std::move(clientHandler)
    );

    auto nHandlers = context.run();

    BOOST_LOG(lg) << "ran " << nHandlers << " handlers, exiting";
}

#include <boost/asio/unyield.hpp>