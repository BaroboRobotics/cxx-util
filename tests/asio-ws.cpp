// Copyright (c) 2014-2016 Barobo, Inc.
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#include <util/doctest.h>

#include <util/asio/iothread.hpp>
#include <util/asio/ws/acceptor.hpp>
#include <util/asio/ws/connector.hpp>

#include <boost/asio/use_future.hpp>

namespace ws = util::asio::ws;
using boost::system::error_code;

auto acceptHandler = [](ws::Acceptor::MessageQueue& mq, auto& buf) {
    util::log::Logger lg;
    return [&, lg](error_code ec) mutable {
        BOOST_LOG(lg) << "server asyncAccept: " << ec.message();
        if (!ec) {
            BOOST_LOG(lg) << "server accepted connection from " << mq.getRemoteEndpoint();
            // Note that our receive buffer MUST outlive the asyncReceive operation. That's why
            // it's on the stack outside this lambda.
            mq.asyncReceive(boost::asio::buffer(buf), [&, lg](error_code ec, size_t nRxBytes) mutable { 
                BOOST_LOG(lg) << "server asyncReceive: " << ec.message();
                if (!ec) {
                    BOOST_LOG(lg) << "server received: "
                        << std::string(buf.data(), buf.data() + nRxBytes);
                    mq.asyncSend(boost::asio::buffer(buf, nRxBytes), [&, lg](error_code ec) mutable {
                        BOOST_LOG(lg) << "server asyncSend: " << ec.message();
                        mq.close(ec);
                        if (ec) {
                            BOOST_LOG(lg) << "server close: " << ec.message();
                        }
                    });
                }
            });
        }
    };
};

TEST_CASE("WebSocket acceptor/connector test") {
    util::asio::IoThread ioThread;

    auto acceptor = ws::Acceptor{ioThread.context()};
    // a `ws::Acceptor` wraps a `websocketpp::server`
    auto serverMq = ws::Acceptor::MessageQueue{ioThread.context()};
    // a `ws::Acceptor::MessageQueue` wraps a `websocketpp::server::connection_ptr`

    auto host = "localhost";
    auto service = "1337";

    //auto query = decltype(resolver)::query(
    //    host, service, boost::asio::ip::resolver_query_base::numeric_service);
    boost::asio::ip::tcp::resolver resolver {ioThread.context()};
    auto epIter = resolver.resolve({host, service});
    acceptor.listen(*epIter);

    std::array<uint8_t, 1024> serverBuffer;
    // Keep the server buffer here on the stack so it outlives all async operations.
    acceptor.asyncAccept(serverMq, acceptHandler(serverMq, serverBuffer));
    // Accept a connection that just echoes a message back to the client and closes the connection.




    auto connector = ws::Connector{ioThread.context()};
    // a `ws::Connector` wraps a `websocketpp::client`
    auto clientMq = ws::Connector::MessageQueue{ioThread.context()};
    // a `ws::Connector::MessageQueue` wraps a `websocketpp::client::connection_ptr`

    auto use_future = boost::asio::use_future_t<std::allocator<char>>{};
    // We need this special use_future to work around an Asio bug on gcc 5+.

    connector.asyncConnect(clientMq, host, service, use_future).get();


    clientMq.asyncSend(boost::asio::buffer("Yo dawg"), use_future).get();

    std::array<uint8_t, 1024> clientBuffer;
    auto nRxBytes = clientMq.asyncReceive(boost::asio::buffer(clientBuffer), use_future).get();

    util::log::Logger lg;
    BOOST_LOG(lg) << "client received "
        << std::string(clientBuffer.data(), clientBuffer.data() + nRxBytes);

    auto ec = error_code{};
    clientMq.close(ec);
    if (ec) { BOOST_LOG(lg) << "client message queue close: " << ec.message(); }
    acceptor.close(ec);
    if (ec) { BOOST_LOG(lg) << "acceptor close: " << ec.message(); }
    connector.close(ec);
    if (ec) { BOOST_LOG(lg) << "connector close: " << ec.message(); }
}
