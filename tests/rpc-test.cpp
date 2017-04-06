#include "./rpc-test-server.hpp"
#include "./rpc-test-client.hpp"

#include <util/overload.hpp>
#include <util/programpath.hpp>

//#include <util/doctest.h>

#include <composed/op.hpp>
#include <composed/phaser.hpp>
#include <composed/async_accept_loop.hpp>
#include <composed/handler_context.hpp>
#include <composed/work_guard.hpp>

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

    handler_type& handler_context;

    boost::asio::signal_set& trap;

    composed::phaser phaser;
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
        : handler_context(h)
        , trap(ss)
        , phaser(ss.get_io_service())
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
    using composed::make_work_guard;
    using composed::bind_handler_context;

    reenter(this) {
        BOOST_LOG(lg) << "start of phase";

        yield {
            auto work = make_work_guard(phaser);

            auto runServer = [this, work](
                    boost::asio::ip::tcp::socket&& s, boost::asio::ip::tcp::endpoint remoteEp) {
                connections.emplace_front(std::move(s));
                auto cleanup = bind_handler_context(handler_context,
                        [this, work, x = connections.begin()] { connections.erase(x); });
                async_server(connections.front(), remoteEp, std::move(cleanup));
            };

            auto cleanup = bind_handler_context(handler_context,
                    [this, work](const boost::system::error_code& ec) {
                        BOOST_LOG(lg) << "accept loop died: " << ec.message();
                        trap.cancel();
                    });

            composed::async_accept_loop(acceptor, runServer, std::move(cleanup));
            // Run an accept loop, handing all newly connected sockets to `runServer`. If the accept
            // loop ever exits, cancel our trap to let the daemon exit.

            async_client(clientStream, serverEndpoint, bind_handler_context(handler_context, []{}));
            // Test things out with a client run. FIXME: this doesn't keep the phase alive with a
            // work_guard.

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

        BOOST_LOG(lg) << "Waiting for end of phase";

        yield return phaser.dispatch(op());

        BOOST_LOG(lg) << "end of phase";
    }
    op.complete();
};

constexpr composed::operation<main_op<>> async_main;

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

    async_main(trap, serverEndpoint, MainHandler{lg});

    auto nHandlers = context.run();
    BOOST_LOG(lg) << "ran " << nHandlers << " handlers, exiting";
}

#include <boost/asio/unyield.hpp>