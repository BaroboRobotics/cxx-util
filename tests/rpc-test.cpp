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

struct main_handler {
    using logger_type = composed::logger;
    logger_type get_logger() const { return p->lg; }

    struct data {
        logger_type lg;
        size_t allocations = 0;
        size_t deallocations = 0;
        size_t allocation_size = 0;
        size_t high_water_mark = 0;
        data(util::log::Logger& l): lg(&l) {}
    };
    std::shared_ptr<data> p;

    explicit main_handler(util::log::Logger& l): p(std::make_shared<data>(l)) {}

    void operator()() {
        BOOST_LOG(p->lg) << "main task complete, "
                << p->allocation_size << " bytes left allocated, "
                << "high water mark: " << p->high_water_mark;
    }

    friend void* asio_handler_allocate(size_t size, main_handler* self) {
        ++self->p->allocations;
        self->p->allocation_size += size;
        self->p->high_water_mark = std::max(self->p->high_water_mark, self->p->allocation_size);

        // Forward to the default implementation of asio_handler_allocate.
        int dummy;
        return beast_asio_helpers::allocate(size, dummy);
    }

    friend void asio_handler_deallocate(void* pointer, size_t size, main_handler* self) {
        ++self->p->deallocations;
        self->p->allocation_size -= size;

        // Forward to the default implementation of asio_handler_deallocate.
        int dummy;
        beast_asio_helpers::deallocate(pointer, size, dummy);
    }

    template <class Function>
    friend void asio_handler_invoke(Function&& function, main_handler* self) {
        // Forward to the default implementation of asio_handler_invoke.
        int dummy;
        beast_asio_helpers::invoke(std::forward<Function>(function), dummy);
    }

    friend bool asio_handler_is_continuation(main_handler* self) {
        return true;
    }
};


template <class Handler = void()>
struct main_op: boost::asio::coroutine {
    using handler_type = Handler;
    using allocator_type = beast::handler_alloc<char, handler_type>;

    using stream_type = beast::websocket::stream<boost::asio::ip::tcp::socket>;

    handler_type& handler_context;

    boost::asio::signal_set trap;

    boost::asio::io_service::strand strand;
    composed::phaser phaser;
    boost::asio::ip::tcp::acceptor acceptor;
    std::list<stream_type/*, allocator_type*/> connections;
    // beast::handler_alloc requires std::allocator_traits usage, but std::list doesn't use
    // std::allocator_traits until gcc-6: https://gcc.gnu.org/bugzilla/show_bug.cgi?id=55409

    boost::asio::ip::tcp::endpoint server_ep;
    stream_type client_stream;

    composed::associated_logger_t<handler_type> lg;
    boost::system::error_code ec;
    int sig_no;

    main_op(handler_type& h, boost::asio::io_service& context,
            boost::asio::ip::tcp::endpoint ep)
        : handler_context(h)
        , trap(context, SIGINT, SIGTERM)
        , strand(context)
        , phaser(strand)
        , acceptor(context, ep)
        , connections(/*allocator_type{h}*/)
        , server_ep(ep)
        , client_stream(context)
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
        yield {
            auto work = make_work_guard(phaser);

            auto run_server = [this, work](
                    boost::asio::ip::tcp::socket&& s, const boost::asio::ip::tcp::endpoint& remote) {
                connections.emplace_front(std::move(s));
                auto cleanup = bind_handler_context(handler_context,
                        [this, work, x = connections.begin()] { connections.erase(x); });
                async_server(connections.front(), remote, std::move(cleanup));
            };

            auto cleanup = bind_handler_context(handler_context,
                    [this, work](const boost::system::error_code& ec) {
                        BOOST_LOG(lg) << "accept loop died: " << ec.message();
                        trap.cancel();
                    });

            composed::async_accept_loop(acceptor, run_server, std::move(cleanup));
            // Run an accept loop, handing all newly connected sockets to `run_server`. If the
            // accept loop ever exits, cancel our trap to let the daemon exit.

            auto discard_results = bind_handler_context(handler_context, [work] {});

            async_client(client_stream, server_ep, std::move(discard_results));
            // Test things out with a client run.

            return trap.async_wait(op(ec, sig_no));
        }

        if (!ec) {
            BOOST_LOG(lg) << "Trap caught signal " << sig_no;
        }
        else {
            BOOST_LOG(lg) << "Trap error: " << ec.message();
        }

        acceptor.close();
        for (auto& con: connections) {
            con.next_layer().close();
        }

        client_stream.next_layer().close();

        yield return phaser.dispatch(op());
    }
    op.complete();
};

constexpr composed::operation<main_op<>> async_main;

namespace po = boost::program_options;

int main(int argc, char** argv) {
    std::string server_host;
    uint16_t server_port;

    auto opts_desc = po::options_description{util::programPath().string() + " command line options"};
    opts_desc.add_options()
        ("help", "display this text")
        ("version", "display version")
        ("host", po::value<std::string>(&server_host)
            ->value_name("<host>")
            ->default_value("0.0.0.0"),
            "bind to this host")
        ("port", po::value<uint16_t>(&server_port)
            ->value_name("<port>")
            ->default_value(17739),
            "bind to this port/service")
    ;

    opts_desc.add(util::log::optionsDescription());

    auto options = boost::program_options::variables_map{};
    po::store(po::parse_command_line(argc, argv, opts_desc), options);
    po::notify(options);

    if (options.count("help")) {
        std::cout << opts_desc << '\n';
        return 0;
    }

    if (options.count("version")) {
        std::cout << util::programPath().string() << " (no version)\n";
        return 0;
    }

    const auto server_ep = boost::asio::ip::tcp::endpoint{
            boost::asio::ip::address::from_string(server_host), server_port};

    util::log::Logger lg;
    boost::asio::io_service context;

    async_main(context, server_ep, main_handler{lg});

    auto n = context.run();
    BOOST_LOG(lg) << "ran " << n << " handlers, exiting";
}

#include <boost/asio/unyield.hpp>
