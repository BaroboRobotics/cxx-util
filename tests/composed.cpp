#include <util/asio/operation.hpp>
#include <util/asio/iothread.hpp>
#include <util/asio/asynccompletion.hpp>

#include <boost/asio.hpp>
#include <boost/asio/yield.hpp> // define reenter, yield, and fork

#include <array>
#include <future>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace asio = boost::asio;
using boost::system::error_code;
using boost::system::system_error;
using std::forward;
using std::move;
using std::string;
using std::cout;
using std::cerr;

static const auto kEchoes = int(100);

template <class Stream>
struct EchoOp {
    EchoOp (Stream& s, int n)
        : stream(s)
        , echoes(n)
    {}

    Stream& stream;
    int echoes;
    std::array<std::uint8_t, 1024> buf;
    error_code rc;

    std::tuple<error_code> result () {
        return std::make_tuple(rc);
    }

    template <class Op>
    void operator() (Op&& op, error_code ec = {}, size_t nTransferred = 0) {
        if (!ec) {
            reenter (op) {
                while (--echoes >= 0) {
                    yield stream.async_read_some(asio::buffer(buf), move(op));
                    yield asio::async_write(stream, asio::buffer(buf, nTransferred), move(op));
                }
            }
        }
        else {
            rc = ec;
        }
    }
};

template <class Stream, class CompletionToken>
BOOST_ASIO_INITFN_RESULT_TYPE(CompletionToken, void(error_code))
asyncEcho (Stream& stream, int echoes, CompletionToken&& token) {
    util::asio::AsyncCompletion<
        CompletionToken, void(error_code)
    > init { forward<CompletionToken>(token) };

    util::asio::v1::makeOperation<EchoOp<Stream>>(std::move(init.handler), stream, echoes)();

    return init.result.get();
}

int main () try {
    util::asio::IoThread io;

    auto endpoint = asio::ip::tcp::endpoint{asio::ip::tcp::v4(), 6666};

    // Server
    auto acceptor = asio::ip::tcp::acceptor{io.context(), endpoint};
    auto serverSocket = asio::ip::tcp::socket{io.context()};
    acceptor.async_accept(serverSocket, [&] (error_code ec) {
        if (!ec) {
            acceptor.close();
            asyncEcho(serverSocket, kEchoes, [&] (error_code ec2) {
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
    auto resolver = asio::ip::tcp::resolver{io.context()};
    auto clientSocket = asio::ip::tcp::socket{io.context()};
    try {
        asio::connect(clientSocket, resolver.resolve({"127.0.0.1", "6666"}));
        cout << "Connected to server\n";
        for (int i = 0; ; ++i) {
            auto s = std::to_string(i);
            (void)asio::write(clientSocket, asio::buffer(s));
            auto v = std::vector<uint8_t>(1024);
            auto nRead = clientSocket.read_some(asio::buffer(v));
            v.resize(nRead);
            auto echoed = string(v.begin(), v.end());
            cout << "Read " << echoed << "\n";
        }
    }
    catch (system_error& e) {
        cout << "Client exception: " << e.what() << "\n";
    }
    auto ec = error_code{};
    clientSocket.shutdown(asio::ip::tcp::socket::shutdown_both, ec);
    clientSocket.close(ec);
    acceptor.close(ec);

    cout << "Ran " << io.join() << " handlers\n";
    return 0;
}
catch (std::exception& e) {
    cerr << "Exception in main(): " << e.what() << "\n";
}

#include <boost/asio/unyield.hpp> // undef reenter, yield, and fork
