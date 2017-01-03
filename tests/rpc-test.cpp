#include <rpc-test.pb.hpp>

#include <util/asio/operation.hpp>
#include <util/log.hpp>
#include <util/overload.hpp>

//#include <util/doctest.h>

#include <beast/websocket.hpp>
#include <boost/asio.hpp>

#include <memory>

#include <boost/asio/yield.hpp>

namespace util {

#if 0
template <class Request, class Reply>
struct RpcRequestBank {
    template <class Handler>
    auto makeRequest(Handler&& handler) {
        auto& requestHandler = requestHandlers[nextRequestId++];
        requestHandler = overloadLinearly(
            std::forward<Handler>(handler),
            [](const auto&) {
                util::log::Logger lg;
                BOOST_LOG(lg) << ""
            }
        );
    }

    auto operator[](uint32_t requestId) {
        return overloadLinearly(
            outstandingRequestHandlers[requestId],
            [requestId, this](const auto&) {
            }
        );
    }
};
#endif

} // util



struct TestServer {
    float propertyValue = 1.0;

    util::log::Logger lg;

    // ===================================================================================
    // RPC request implementations

    rpc_test_GetProperty_Out operator()(const rpc_test_GetProperty_In& in);
    rpc_test_SetProperty_Out operator()(const rpc_test_SetProperty_In& in);

    // ===================================================================================
    // Event messages

    void operator()(const rpc_test_RpcRequest& rpcRequest);
    void operator()(const rpc_test_Quux&);
};

struct TestClient {
    //util::RpcRequestBank<rpc_test_RpcRequest, rpc_test_RpcReply> requestBank;

    util::log::Logger lg;

    // ===================================================================================
    // Event messages

    void operator()(const rpc_test_RpcReply& rpcReply);
    void operator()(const rpc_test_Quux& quux);
};



rpc_test_GetProperty_Out TestServer::operator()(const rpc_test_GetProperty_In& in) {
    BOOST_LOG(lg) << "Server received a GetProperty RPC request";
    return {true, propertyValue};
}

rpc_test_SetProperty_Out TestServer::operator()(const rpc_test_SetProperty_In& in) {
    if (in.has_value) {
        propertyValue = in.value;
    }
    else {
        BOOST_LOG(lg) << "Server received an invalid SetProperty RPC request";
    }
    return {};
}

void TestServer::operator()(const rpc_test_RpcRequest& rpcRequest) {
    auto serverToClient = rpc_test_ServerToClient{};
    serverToClient.arg.rpcReply.has_requestId = rpcRequest.has_requestId;
    serverToClient.arg.rpcReply.requestId = rpcRequest.requestId;

    auto visitor = [&serverToClient, this](const auto& req) {
        nanopb::assign(serverToClient.arg.rpcReply.arg, (*this)(req));
        nanopb::assign(serverToClient.arg, serverToClient.arg.rpcReply);
    };
    if (nanopb::visit(visitor, rpcRequest.arg)) {
        //static_assert(false, "TODO: send serverToClient back to client");
        BOOST_LOG(lg) << "Server received and replied to an RPC request";
    }
    else {
        BOOST_LOG(lg) << "Server received an unrecognized RPC request";
    }
}

void TestServer::operator()(const rpc_test_Quux&) {
    BOOST_LOG(lg) << "Server received a Quux event";
}



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

template <class DynamicBuffer>
pb_ostream_t nanopbOStreamFromDynabuf(DynamicBuffer& dynabuf) {
    auto writer = [](pb_ostream_t* stream, const uint8_t* buf, size_t count) {
        auto& dynabufRef = *static_cast<DynamicBuffer*>(stream->state);
        beast::write(dynabufRef, boost::asio::buffer(buf, count));
        return true;
    };
    return {writer, &dynabuf, dynabuf.max_size() - dynabuf.size(), 0};
}


int main () {
    util::log::Logger lg;
    boost::asio::io_service context;

    const auto serverEndpoint = boost::asio::ip::tcp::endpoint{
            boost::asio::ip::address::from_string("127.0.0.1"), 17739};

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
                    BOOST_LOG(op.log()) << "Closing server after signal " << sigNo;
                    acceptor.close(ec);
                    BOOST_LOG(op.log()) << "acceptor closed: " << ec.message();
                    ws.next_layer().close(ec);
                    // TODO: more correct way of closing?
                    BOOST_LOG(op.log()) << "server WebSocket closed: " << ec.message();
                }
                return;
            }

            yield acceptor.async_accept(ws.next_layer(), clientEndpoint, std::move(op));
            if (ec) { BOOST_LOG(op.log()) << ec.message(); op.complete(ec); return; }

            BOOST_LOG(op.log()) << "Server accepted TCP connection from " << clientEndpoint;

            yield ws.async_accept(std::move(op));
            if (ec) { BOOST_LOG(op.log()) << ec.message(); op.complete(ec); return; }

            BOOST_LOG(op.log()) << "Server accepted WebSocket connection from " << clientEndpoint;

            ws.set_option(beast::websocket::message_type{beast::websocket::opcode::binary});
            BOOST_LOG(op.log()) << "&op=[" << &op << "]";
#if 1
            // To close the connection, we're supposed to call (async_)close, then read until we get
            // an error.
            yield ws.async_close({"Hodor indeed!"}, /*std::move(*/op/*)*/);
            if (ec) { BOOST_LOG(op.log()) << ec.message(); op.complete(ec); return; }

            do {
                BOOST_LOG(op.log()) << "Server reading ...";
                buf.consume(buf.size());
                yield ws.async_read(opcode, buf, /*std::move(*/op/*)*/);
            } while (!ec);

            if (ec == beast::websocket::error::closed) {
                BOOST_LOG(op.log()) << "Server WebSocket closed, client gave code/reason: "
                        << ws.reason().code << '/' << ws.reason().reason.c_str();
            }
            else {
                BOOST_LOG(op.log()) << "Server read error: " << ec.message();
            }
#endif
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
                    BOOST_LOG(op.log()) << "Closing client after signal " << sigNo;
                    ws.next_layer().close(ec);
                    // TODO: more correct way of closing?
                    BOOST_LOG(op.log()) << "client WebSocket closed: " << ec.message();
                }
                return;
            }

            yield ws.next_layer().async_connect(serverEndpoint, std::move(op));
            if (ec) { BOOST_LOG(op.log()) << ec.message(); op.complete(ec); return; }

            BOOST_LOG(op.log()) << "Client established TCP connection to " << serverEndpoint;

            yield ws.async_handshake("hodorhodorhodor.com", "/cgi-bin/hax", std::move(op));
            if (ec) { BOOST_LOG(op.log()) << ec.message(); op.complete(ec); return; }

            BOOST_LOG(op.log()) << "Client established WebSocket connection to " << serverEndpoint;

            ws.set_option(beast::websocket::message_type{beast::websocket::opcode::binary});
#if 0
            yield {
                auto clientToServer = rpc_test_ClientToServer{};
                nanopb::assign(clientToServer.arg, clientToServer.arg.quux);

                auto ostream = nanopbOStreamFromDynabuf(buf);
                if (!nanopb::encode(ostream, clientToServer)) {
                    BOOST_LOG(op.log()) << "Client encoding failure";
                    buf.consume(buf.size());
                }
                ws.async_write(buf.data(), std::move(op));
            }
            buf.consume(buf.size());
#endif
            // To close the connection, we're supposed to call (async_)close, then read until we get
            // an error.

            yield ws.async_close({"Hodor!"}, /*std::move(*/op/*)*/);
            if (ec) { BOOST_LOG(op.log()) << ec.message(); op.complete(ec); return; }

            do {
                BOOST_LOG(op.log()) << "Client reading ...";
                buf.consume(buf.size());
                yield ws.async_read(opcode, buf, /*std::move(*/op/*)*/);
            } while (!ec);

            if (ec == beast::websocket::error::closed) {
                BOOST_LOG(op.log()) << "Client WebSocket closed, server gave code/reason: "
                        << ws.reason().code << '/' << ws.reason().reason.c_str();
            }
            else {
                BOOST_LOG(op.log()) << "Client read error: " << ec.message();
            }

            terminator->cancel();
            op.complete(ec);
        }
    };

    auto serverHandler = [lg](boost::system::error_code ec) mutable {
        if (!ec) {
            BOOST_LOG(lg) << "Server task completed successfully";
        }
        else {
            BOOST_LOG(lg) << "Server task completed: " << ec.message();
        }
    };

    auto clientHandler = [lg](boost::system::error_code ec) mutable {
        if (!ec) {
            BOOST_LOG(lg) << "Client task completed successfully";
        }
        else {
            BOOST_LOG(lg) << "Client task completed: " << ec.message();
        }
    };

    util::asio::asyncDispatch(
        context,
        std::make_tuple(make_error_code(boost::asio::error::operation_aborted)),
        std::move(clientTask),
        util::asio::addAssociatedLogger(std::move(clientHandler), util::log::Logger{})
    );

    util::asio::asyncDispatch(
        context,
        std::make_tuple(make_error_code(boost::asio::error::operation_aborted)),
        std::move(serverTask),
        util::asio::addAssociatedLogger(std::move(serverHandler), util::log::Logger{})
    );

    auto nHandlers = context.run();

    BOOST_LOG(lg) << "ran " << nHandlers << " handlers, exiting";
}

#include <boost/asio/unyield.hpp>