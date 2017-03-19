#include "./rpc-test-server.hpp"
#include "./rpc-test-client.hpp"

#include <util/overload.hpp>
#include <util/programpath.hpp>

//#include <util/doctest.h>

#include <composed/op_logger.hpp>
#include <composed/phaser.hpp>
#include <composed/async_accept_loop.hpp>

#include <beast/websocket.hpp>
#include <beast/core/handler_alloc.hpp>
#include <boost/asio.hpp>

#include <boost/program_options/parsers.hpp>

#include <iostream>
#include <memory>

#include <boost/asio/yield.hpp>

// =======================================================================================
// Main

struct MainHandler {
    using logger_type = composed::logger;
    logger_type get_logger() const { return p->lg; }

    struct Data {
        logger_type lg;
        size_t allocations = 0;
        size_t deallocations = 0;
        size_t allocation_size = 0;
        size_t high_water_mark = 0;
        Data(util::log::Logger& l): lg(&l) {}
    };
    std::shared_ptr<Data> p;

    explicit MainHandler(util::log::Logger& l): p(std::make_shared<Data>(l)) {}

    void operator()() {
        BOOST_LOG(p->lg) << "main task complete, "
                << p->allocation_size << " bytes left allocated, "
                << "high water mark: " << p->high_water_mark;
    }

    friend void* asio_handler_allocate(size_t size, MainHandler* self) {
        ++self->p->allocations;
        self->p->allocation_size += size;
        self->p->high_water_mark = std::max(self->p->high_water_mark, self->p->allocation_size);

        // Forward to the default implementation of asio_handler_allocate.
        int dummy;
        return beast_asio_helpers::allocate(size, dummy);
    }

    friend void asio_handler_deallocate(void* pointer, size_t size, MainHandler* self) {
        ++self->p->deallocations;
        self->p->allocation_size -= size;

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
struct main_op: boost::asio::coroutine {
    using handler_type = Handler;
    using allocator_type = beast::handler_alloc<char, handler_type>;

    using WsStream = beast::websocket::stream<boost::asio::ip::tcp::socket>;

    boost::asio::signal_set& trap;

    composed::phaser<handler_type> phaser;
    boost::asio::ip::tcp::acceptor acceptor;
    std::list<WsStream/*, allocator_type*/> connections;
    // beast::handler_alloc requires std::allocator_traits usage, but std::list doesn't use
    // std::allocator_traits until gcc-6: https://gcc.gnu.org/bugzilla/show_bug.cgi?id=55409

    boost::asio::ip::tcp::endpoint serverEndpoint;
    WsStream clientStream;

    composed::associated_logger_t<handler_type> lg;
    boost::system::error_code ec;
    int sigNo;

    main_op(handler_type& h, boost::asio::signal_set& ss, boost::asio::ip::tcp::endpoint ep)
        : trap(ss)
        , phaser(ss.get_io_service(), h)
        , acceptor(ss.get_io_service(), ep)
        , connections(/*allocator_type{h}*/)
        , serverEndpoint(ep)
        , clientStream(ss.get_io_service())
        , lg(composed::get_associated_logger(h))
    {
        lg.add_attribute("Role", boost::log::attributes::make_constant("main"));
    }

    void operator()(composed::op<main_op>&);
};

template <class Handler>
void main_op<Handler>::operator()(composed::op<main_op>& op) {
    reenter(this) {
        yield {
            auto runServer = [this](
                    boost::asio::ip::tcp::socket&& s, boost::asio::ip::tcp::endpoint remoteEp) {
                connections.emplace_front(std::move(s));
                auto cleanup = [this, x = connections.begin()] { connections.erase(x); };
                composed::async_run<server_op<>>(
                        connections.front(),
                        remoteEp,
                        phaser.completion(cleanup));
            };

            auto cleanup = [this](const boost::system::error_code& ec) {
                BOOST_LOG(lg) << "accept loop died: " << ec.message();
                trap.cancel();
            };

            composed::async_accept_loop(acceptor, runServer, phaser.completion(cleanup));
            // Run an accept loop, handing all newly connected sockets to `runServer`. If the accept
            // loop ever exits, cancel our trap to let the daemon exit.

            composed::async_run<client_op<>>(clientStream, serverEndpoint, phaser.completion());
            // Test things out with a client run.

            return trap.async_wait(op(ec, sigNo));
        }

        if (!ec) {
            BOOST_LOG(lg) << "Trap caught signal " << sigNo;
        }
        else {
            BOOST_LOG(lg) << "Trap error: " << ec.message();
        }

        acceptor.close();
        for (auto& con: connections) {
            con.next_layer().close();
        }

        clientStream.next_layer().close();

        yield return phaser.async_wait(op(ec));
    }
    op.complete();
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