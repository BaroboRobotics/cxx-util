#include <util/composedop.hpp>

#include <boost/asio.hpp>

#include <boost/optional.hpp>
#include <boost/utility/in_place_factory.hpp>

#include <array>
#include <future>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

using namespace std;
namespace asio = boost::asio;
namespace sys = boost::system;

//static const uint8_t kSyncMessage [] = { Cmnd_STK_GET_SYNC, Sync_CRC_EOP };

static const auto kEchoes = int(100);

#include <boost/asio/yield.hpp> // define reenter, yield, and fork
template <class Stream>
struct EchoOp {
    EchoOp (Stream& s, int n)
        : stream(s)
        , echoes(n)
    {}

    Stream& stream;
    int echoes;
    array<uint8_t, 1024> buf;

    template <class Op>
    void operator() (Op& op, sys::error_code ec, size_t nTransferred) {
        if (!ec) {
            reenter (op) {
                while (--echoes >= 0) {
                    yield stream.async_read_some(asio::buffer(buf), move(op));
                    yield asio::async_write(stream, asio::buffer(buf, nTransferred), move(op));
                }
                op.complete(sys::error_code{});
            }
        }
        else {
            op.complete(ec);
        }
    }
};
#include <boost/asio/unyield.hpp> // undef reenter, yield, and fork

typedef void EchoHandlerSignature(sys::error_code);

template <class Stream, class CompletionToken>
BOOST_ASIO_INITFN_RESULT_TYPE(CompletionToken, EchoHandlerSignature)
asyncEcho (Stream& stream, int echoes, CompletionToken&& token) {
    asio::detail::async_result_init<
        CompletionToken, EchoHandlerSignature
    > init { forward<CompletionToken>(token) };

    //startComposedOp(echoOpImpl, EchoOp<Stream>{stream, echoes}, init.handler);
    util::ComposedOp<EchoOp<Stream>, decltype(init.handler)>{
        EchoOp<Stream>{stream, echoes}, init.handler
    }(sys::error_code{}, 0);

    return init.result.get();
}

int main () {
    auto ios = asio::io_service{};
    auto work = boost::optional<asio::io_service::work>{boost::in_place(std::ref(ios))};
    auto nHandlers = async(launch::async, [&] () { return ios.run(); });

    auto endpoint = asio::ip::tcp::endpoint{asio::ip::tcp::v4(), 6666};

    // Server
    auto acceptor = asio::ip::tcp::acceptor{ios, endpoint};
    auto serverSocket = asio::ip::tcp::socket{ios};
    acceptor.async_accept(serverSocket, [&] (sys::error_code ec) {
        if (!ec) {
            acceptor.close();
            asyncEcho(serverSocket, kEchoes, [&] (sys::error_code ec2) {
                cout << "Echo server finished, woot: " << ec2.message() << "\n";
                serverSocket.shutdown(asio::ip::tcp::socket::shutdown_both, ec2);
                serverSocket.close(ec2);
            });
        }
        else {
            cout << "accept failed: " << ec.message() << "\n";
        }
    });

    // Client
    auto resolver = asio::ip::tcp::resolver{ios};
    auto clientSocket = asio::ip::tcp::socket{ios};
    try {
        asio::connect(clientSocket, resolver.resolve({"127.0.0.1", "6666"}));
        cout << "Connected to server\n";
        for (int i = 0; ; ++i) {
            auto s = to_string(i);
            (void)asio::write(clientSocket, asio::buffer(s));
            auto v = vector<uint8_t>(1024);
            auto nRead = clientSocket.read_some(asio::buffer(v));
            v.resize(nRead);
            auto echoed = string(v.begin(), v.end());
            cout << "Read " << echoed << "\n";
        }
    }
    catch (sys::system_error& e) {
        cout << "Client exception: " << e.what() << "\n";
    }
    auto ec = sys::error_code{};
    clientSocket.shutdown(asio::ip::tcp::socket::shutdown_both, ec);
    clientSocket.close(ec);
    acceptor.close(ec);

    work = boost::none;
    cout << "Ran " << nHandlers.get() << " handlers\n";
    return 0;
}
