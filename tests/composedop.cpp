#include <util/asio_handler_helpers.hpp>

#include <boost/asio.hpp>
#include <boost/asio/coroutine.hpp>

#include <boost/optional.hpp>
#include <boost/utility/in_place_factory.hpp>

#include <array>
#include <future>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace std;
namespace asio = boost::asio;
namespace sys = boost::system;

//static const uint8_t kSyncMessage [] = { Cmnd_STK_GET_SYNC, Sync_CRC_EOP };

static const auto kEchoes = int(10);

typedef void EchoHandlerSignature(sys::error_code);

#include <boost/asio/yield.hpp> // define reenter, yield, and fork
template <class Stream, class Handler>
struct EchoOp {
    struct State {
        template <class H>
        State (Stream& s, int n, H&& h)
            : stream(s)
            , echoes(n)
            , handler(forward<H>(h))
        {}

        Stream& stream;
        int echoes;
        Handler handler;
        array<uint8_t, 1024> buf;
    };

    asio::coroutine coro;
    bool continuation = false;
    shared_ptr<State> m;

    template <class H>
    EchoOp (Stream& stream, int echoes, H&& h)
        : m(make_shared<State>(stream, echoes, forward<H>(h)))
    {}

    void operator() (sys::error_code ec, size_t nTransferred, bool c = true) {
        if (!ec) {
            continuation = c;
            reenter (coro) {
                while (--m->echoes >= 0) {
                    yield m->stream.async_read_some(asio::buffer(m->buf), move(*this));
                    yield asio::async_write(m->stream, asio::buffer(m->buf, nTransferred), move(*this));
                }
                m->handler(sys::error_code{});
            }
        }
        else {
            m->handler(ec);
        }
    }

    // Inherit the allocation and invocation strategies from the operation's
    // completion handler.
    friend void* asio_handler_allocate (size_t size, EchoOp* self) {
        return util::asio_handler_helpers::allocate(size, self->m->handler);
    }

    friend void asio_handler_deallocate (void* pointer, size_t size, EchoOp* self) {
        util::asio_handler_helpers::deallocate(pointer, size, self->m->handler);
    }

    template <class Function>
    friend void asio_handler_invoke (Function&& f, EchoOp* self) {
        util::asio_handler_helpers::invoke(forward<Function>(f), self->m->handler);
    }

    // Inherit the is_continuation strategy from the operation's completion
    // handler unless we know we are a continuation (i.e., the coroutine is
    // reentered, then suspends itself again).
    friend bool asio_handler_is_continuation (EchoOp* self) {
        return self->continuation || util::asio_handler_helpers::is_continuation(self->m->handler)
    }
};
#include <boost/asio/unyield.hpp> // undef reenter, yield, and fork

template <class Stream, class CompletionToken>
BOOST_ASIO_INITFN_RESULT_TYPE(CompletionToken, EchoHandlerSignature)
asyncEcho (Stream& stream, int echoes, CompletionToken&& token) {
    asio::detail::async_result_init<
        CompletionToken, EchoHandlerSignature
    > init { forward<CompletionToken>(token) };

    auto op = EchoOp<Stream, decltype(init.handler)>{stream, echoes, init.handler};
    op(sys::error_code{}, 0, false);

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
