#include <util/doctest.h>
#include <util/log.hpp>

#include <composed/op_logger.hpp>
#include <composed/timed.hpp>
#include <composed/signalled.hpp>
#include <composed/object.hpp>

#include <beast/core/async_completion.hpp>
#include <beast/core/handler_alloc.hpp>

#include <boost/asio.hpp>
#include <boost/asio/steady_timer.hpp>

#include <boost/scope_exit.hpp>

#include <chrono>
#include <tuple>

#include <csignal>

#include <boost/asio/yield.hpp>

struct test_handler {
    // A final handler to aid in checking that all the handler hooks are being exercised correctly.

    const bool cont;

    std::shared_ptr<util::log::Logger> lgp;
    // Wrap the logger in a shared_ptr to make sure test_handler is move-constructible.

    using logger_type = composed::logger;
    logger_type lg;
    logger_type get_logger() const { return lg; }

    explicit test_handler(const std::string& role, bool c = false)
            : cont(c), lgp(std::make_shared<util::log::Logger>()), lg(lgp.get()) {
        lg.add_attribute("Role", boost::log::attributes::constant<std::string>(role));
    }

    size_t allocations = 0;
    size_t deallocations = 0;
    std::map<void*, size_t> allocation_table;

    static thread_local bool invoked_on_my_context;

    void operator()(boost::system::error_code ec, int count) {
        CHECK(allocations > 0);
        // There was at least one allocation.

        CHECK(allocation_table.empty());
        CHECK(deallocations == allocations);
        // But they were all deallocated before the handler was invoked.

        BOOST_LOG(lg) << "test_handler: " << count << " (" << ec.message() << ")";
    }

    friend void* asio_handler_allocate(size_t size, test_handler* self) {
        // Forward to the default implementation of asio_handler_allocate.
        int dummy;
        auto p = beast_asio_helpers::allocate(size, dummy);

        bool inserted;
        std::tie(std::ignore, inserted) = self->allocation_table.insert(std::make_pair(p, size));
        CHECK(inserted);
        // This allocation is new.

        ++self->allocations;

        //BOOST_LOG(self->lg) << "allocated " << p << " (" << size << " bytes)";

        return p;
    }

    friend void asio_handler_deallocate(void* pointer, size_t size, test_handler* self) {
        //BOOST_LOG(self->lg) << "deallocating " << pointer << " (" << size << " bytes)";

        auto it = self->allocation_table.find(pointer);
        REQUIRE(it != self->allocation_table.end());
        // This allocation exists.

        CHECK(size == it->second);
        // And it is the stipulated size.

        self->allocation_table.erase(it);
        ++self->deallocations;

        // Forward to the default implementation of asio_handler_deallocate.
        int dummy;
        beast_asio_helpers::deallocate(pointer, size, dummy);
    }

    template <class Function>
    friend void asio_handler_invoke(Function&& function, test_handler* self) {
        BOOST_SCOPE_EXIT_ALL(&) {
            test_handler::invoked_on_my_context = false;
        };
        test_handler::invoked_on_my_context = true;
        // Composed operations can check this thread_local to ensure that their continuations were
        // invoked through this function.

        // Forward to the default implementation of asio_handler_invoke.
        int dummy;
        beast_asio_helpers::invoke(std::forward<Function>(function), dummy);
    }

    friend bool asio_handler_is_continuation(test_handler* self) {
        return self->cont;
    }
};

thread_local bool test_handler::invoked_on_my_context = false;

// =======================================================================================
// A composed operation implementation

// SomeType exists just to test that the COMPOSED_OP macro correctly forwards extra template
// arguments, whether or not the user passes them.
template <class Handler = void(boost::system::error_code, int), class SomeType = int>
struct test_op {
    using handler_type = Handler;
    using allocator_type = beast::handler_alloc<char, handler_type>;

    boost::asio::steady_timer& timer;
    boost::asio::coroutine coro;
    boost::system::error_code ec;
    int count = 0;
    constexpr static int k_max_count = 10;

    std::vector<int, allocator_type> v;

    composed::associated_logger_t<handler_type> lg;

    test_op(handler_type& h, boost::asio::steady_timer& t)
            : timer(t), v(allocator_type(h)), lg(composed::get_associated_logger(h)) {}

    void operator()(composed::op<test_op>& op);
};

template <class Handler, class Int>
void test_op<Handler, Int>::operator()(composed::op<test_op>& op) {
    auto role = boost::log::attribute_cast<boost::log::attributes::constant<std::string>>(
            lg.get_attributes()["Role"]);
    //CHECK(role.get() == "struct");

    BOOST_LOG(lg) << "Task: " << count;
    if (!ec) reenter (coro) {
        while (++count < k_max_count) {
            timer.expires_from_now(std::chrono::milliseconds(100));
            yield return timer.async_wait(op(ec));
            CHECK(test_handler::invoked_on_my_context);
            v.insert(v.end(), 1024, count);
        }
    }
    op.complete(ec, count);
}

// You can define an initiating function the traditional way, using start_op to start the op:
template <class Token>
auto async_test(boost::asio::steady_timer& t, Token&& token) {
    beast::async_completion<Token, void(boost::system::error_code, int)> init{token};
    composed::start_op<test_op<decltype(init.handler)>>(std::move(init.handler), t);
    return init.result.get();
}
// or you can also use: composed::async_run<test_op<>>(t, token);

// =======================================================================================
// Test cases

TEST_CASE("can start an op") {
    boost::asio::io_service context;
    boost::asio::steady_timer timer{context};

    SUBCASE("with a async_completion initiating function") {
        async_test(timer, test_handler{"async_completion"});
        context.run();
    }

    SUBCASE("with an async_run initiating function") {
        using composed::async_run;
        async_run<test_op<>>(timer, test_handler{"async_run"});
        context.run();
    }

    SUBCASE("with an operation object") {
        constexpr composed::operation<test_op<>> async_test2;
        async_test2(timer, test_handler{"operation"});
        context.run();
    }
}

TEST_CASE("can set timed expirations on operations") {
    using namespace std::literals::chrono_literals;
    boost::asio::io_service context;
    boost::asio::steady_timer timer{context};

    SUBCASE("absolute") {
        async_test(timer, composed::timed(timer, std::chrono::steady_clock::now() + 1150ms,
                test_handler{"timed-absolute"}));
        context.run();
    }

    SUBCASE("relative") {
        async_test(timer, composed::timed(timer, 150ms,
                test_handler{"timed-relative"}));
        context.run();
    }
}

TEST_CASE("can set signalled expirations on operations") {
    boost::asio::io_service context;
    boost::asio::steady_timer timer{context};

    async_test(timer, composed::signalled(timer, composed::make_array(SIGINT, SIGTERM),
            test_handler{"signalled"}));
    std::raise(SIGTERM);
    context.run();
}

TEST_CASE("can set timeouts yet another way") {
    using namespace std::literals::chrono_literals;
    boost::asio::io_service context;
    boost::asio::steady_timer timer{context};
    boost::asio::steady_timer timeout_timer{context, 150ms};

    auto j = composed::make_timeout_joiner(
            timer, timeout_timer, test_handler{"another-timeout"});
    timeout_timer.async_wait(composed::branch(j, composed::timeout_tag{}));
    async_test(timer, composed::branch(j));
    context.run();
}

#include <boost/asio/unyield.hpp>
