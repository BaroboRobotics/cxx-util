#include <util/doctest.h>
#include <util/log.hpp>

#include <composed/op.hpp>
#include <composed/timed.hpp>
#include <composed/signalled.hpp>
#include <composed/handler_context.hpp>
#include <composed/work_guard.hpp>
#include <composed/phaser.hpp>

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

    struct data {
        explicit data(bool c): cont(c) {}
        bool cont;

        size_t completion_count = 0;

        size_t get_logger_count = 0;
        size_t allocate_count = 0;
        size_t deallocate_count = 0;
        size_t invoke_count = 0;
        size_t is_continuation_count = 0;

        util::log::Logger lg;
        std::map<void*, size_t> allocation_table;
    };

    std::shared_ptr<data> d;

    using logger_type = composed::logger;
    logger_type lg;
    logger_type get_logger() const {
        ++d->get_logger_count;
        return logger_type{&d->lg};
    }

    explicit test_handler(const std::string& role, bool c = false)
            : d(std::make_shared<data>(c)) {
        d->lg.add_attribute("Role", boost::log::attributes::make_constant(role));
    }

    template <class... Args>
    void operator()(Args&&...) {
        ++d->completion_count;
        // Check the deallocation-before-allocation guarantee.
        CHECK(d->allocation_table.empty());
        CHECK(d->deallocate_count == d->allocate_count);
    }

    friend void* asio_handler_allocate(size_t size, test_handler* self) {
        // Forward to the default implementation of asio_handler_allocate.
        int dummy;
        using boost::asio::asio_handler_allocate;
        auto p = asio_handler_allocate(size, &dummy);

        bool inserted;
        std::tie(std::ignore, inserted) = self->d->allocation_table.insert(std::make_pair(p, size));
        CHECK(inserted);
        // This allocation is new.

        ++self->d->allocate_count;

        return p;
    }

    friend void asio_handler_deallocate(void* pointer, size_t size, test_handler* self) {
        //BOOST_LOG(self->lg) << "deallocating " << pointer << " (" << size << " bytes)";

        auto it = self->d->allocation_table.find(pointer);
        REQUIRE(it != self->d->allocation_table.end());
        // This allocation exists.

        CHECK(size == it->second);
        // And it is the stipulated size.

        self->d->allocation_table.erase(it);
        ++self->d->deallocate_count;

        // Forward to the default implementation of asio_handler_deallocate.
        int dummy;
        using boost::asio::asio_handler_deallocate;
        asio_handler_deallocate(pointer, size, &dummy);
    }

    template <class Function>
    friend void asio_handler_invoke(Function&& function, test_handler* self) {
        // TODO: hold a mutex lock here so we can test that intermediate handlers are indeed invoked
        // on a given context.

        ++self->d->invoke_count;

        // Forward to the default implementation of asio_handler_invoke.
        int dummy;
        using boost::asio::asio_handler_invoke;
        asio_handler_invoke(std::forward<Function>(function), &dummy);
    }

    friend bool asio_handler_is_continuation(test_handler* self) {
        ++self->d->is_continuation_count;
        return self->d->cont;
    }
};

// =======================================================================================
// A composed operation implementation

// SomeType exists just to test that the COMPOSED_OP macro correctly forwards extra template
// arguments, whether or not the user passes them.
template <class Handler = void(boost::system::error_code, int), class SomeType = int>
struct test_op: boost::asio::coroutine {
    using handler_type = Handler;
    using allocator_type = beast::handler_alloc<char, handler_type>;

    boost::asio::steady_timer& timer;
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
    if (!ec) reenter(this) {
        while (++count < k_max_count) {
            timer.expires_from_now(std::chrono::milliseconds(100));
            yield return timer.async_wait(op(ec));
            //CHECK(test_handler::invoked_on_my_context);
            v.insert(v.end(), 1024, count);
        }
    }
    op.complete(ec, count);
}

constexpr composed::operation<test_op<>> async_test;

// =======================================================================================
// Test cases

TEST_CASE("can start a composed::op") {
    boost::asio::io_service context;
    boost::asio::steady_timer timer{context};

    SUBCASE("with composed::async_run initiating function template") {
        composed::async_run<test_op<>>(timer, test_handler{"async_run"});
        context.run();
    }

    SUBCASE("with composed::operation initiating function object") {
        async_test(timer, test_handler{"operation"});
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

// =======================================================================================
// phaser

TEST_CASE("phaser dispatches handlers in FIFO order") {
    boost::asio::io_service context;
    boost::asio::io_service::strand strand{context};
    composed::phaser<boost::asio::io_service::strand&> phaser{strand};

    int i = 0;

    phaser.dispatch([&] {
        CHECK(i == 0);
        ++i;
        phaser.dispatch([&] {
            CHECK(i == 1);
            ++i;
            phaser.dispatch([&] {
                CHECK(i == 4);
                ++i;
            });
        });
        phaser.dispatch([&] {
            CHECK(i == 2);
            ++i;
        });
        phaser.dispatch([&] {
            CHECK(i == 3);
            ++i;
        });
    });

    context.run();

    CHECK(i == 5);
}

// =======================================================================================
// handler_context

TEST_CASE("can adopt another handler's execution/allocation/etc context") {
    boost::asio::io_service context;
    boost::asio::steady_timer timer{context};

    test_handler f{"adoptee"};
    test_handler h{"adopted-context"};

    async_test(timer, composed::bind_handler_context(h, f));
    context.run();

    // The adoptee handler got the upcall, but its context was untouched.
    CHECK(f.d->completion_count == 1);
    CHECK(f.d->get_logger_count == 0);
    CHECK(f.d->allocate_count == 0);
    CHECK(f.d->deallocate_count == 0);
    CHECK(f.d->invoke_count == 0);
    CHECK(f.d->is_continuation_count == 0);

    // The adopted handler's context was used, but it didn't get the upcall.
    CHECK(h.d->completion_count == 0);
    CHECK(h.d->get_logger_count > 0);
    CHECK(h.d->allocate_count > 0);
    CHECK(h.d->deallocate_count > 0);
    CHECK(h.d->invoke_count > 0);
    CHECK(h.d->is_continuation_count > 0);
}

// =======================================================================================
// work_guard

struct test_work_guard {
    size_t work_started = 0;
    size_t work_finished = 0;
    void on_work_started() { ++work_started; }
    void on_work_finished() { ++work_finished; }
};

TEST_CASE("work_guard") {
    test_work_guard exec;

    SUBCASE("is default constructible") {
        composed::work_guard<test_work_guard> w;
    }

    SUBCASE("can be constructed by make_work_guard") {
        {
            auto w = composed::make_work_guard(exec);
            CHECK(exec.work_started == 1);
            CHECK(exec.work_finished == 0);
        }
        CHECK(exec.work_started == 1);
        CHECK(exec.work_finished == 1);
    }

    SUBCASE("can be copy-constructed") {
        {
            auto w = composed::make_work_guard(exec);
            CHECK(exec.work_started == 1);
            CHECK(exec.work_finished == 0);

            {
                composed::work_guard<test_work_guard> w2{w};
                CHECK(exec.work_started == 2);
                CHECK(exec.work_finished == 0);
            }
            CHECK(exec.work_started == 2);
            CHECK(exec.work_finished == 1);
        }
        CHECK(exec.work_started == 2);
        CHECK(exec.work_finished == 2);
    }

    SUBCASE("can be copy-assigned to owned work") {
        {
            auto w = composed::make_work_guard(exec);
            CHECK(exec.work_started == 1);
            CHECK(exec.work_finished == 0);

            {
                auto w2 = composed::make_work_guard(exec);
                CHECK(exec.work_started == 2);
                CHECK(exec.work_finished == 0);

                w2 = w;
                CHECK(exec.work_started == 3);
                CHECK(exec.work_finished == 1);
            }
            CHECK(exec.work_started == 3);
            CHECK(exec.work_finished == 2);
        }
        CHECK(exec.work_started == 3);
        CHECK(exec.work_finished == 3);
    }

    SUBCASE("can be copy-assigned to unowned work") {
        {
            auto w = composed::make_work_guard(exec);
            CHECK(exec.work_started == 1);
            CHECK(exec.work_finished == 0);

            {
                composed::work_guard<test_work_guard> w2;
                CHECK(exec.work_started == 1);
                CHECK(exec.work_finished == 0);

                w2 = w;
                CHECK(exec.work_started == 2);
                CHECK(exec.work_finished == 0);
            }
            CHECK(exec.work_started == 2);
            CHECK(exec.work_finished == 1);
        }
        CHECK(exec.work_started == 2);
        CHECK(exec.work_finished == 2);
    }

    SUBCASE("can be copy-assigned to self") {
        {
            auto w = composed::make_work_guard(exec);
            CHECK(exec.work_started == 1);
            CHECK(exec.work_finished == 0);

            w = w;
            CHECK(exec.work_started == 2);
            CHECK(exec.work_finished == 1);
        }
        CHECK(exec.work_started == 2);
        CHECK(exec.work_finished == 2);
    }

    SUBCASE("can be move-constructed") {
        {
            auto w = composed::make_work_guard(exec);
            CHECK(exec.work_started == 1);
            CHECK(exec.work_finished == 0);

            {
                composed::work_guard<test_work_guard> w2{std::move(w)};
                CHECK(exec.work_started == 1);
                CHECK(exec.work_finished == 0);
            }
            CHECK(exec.work_started == 1);
            CHECK(exec.work_finished == 1);
        }
        CHECK(exec.work_started == 1);
        CHECK(exec.work_finished == 1);
    }

    SUBCASE("can be move-assigned to owned work") {
        {
            auto w = composed::make_work_guard(exec);
            CHECK(exec.work_started == 1);
            CHECK(exec.work_finished == 0);

            {
                auto w2 = composed::make_work_guard(exec);
                CHECK(exec.work_started == 2);
                CHECK(exec.work_finished == 0);

                w2 = std::move(w);
                CHECK(exec.work_started == 2);
                CHECK(exec.work_finished == 1);
            }
            CHECK(exec.work_started == 2);
            CHECK(exec.work_finished == 2);
        }
        CHECK(exec.work_started == 2);
        CHECK(exec.work_finished == 2);
    }

    SUBCASE("can be move-assigned to unowned work") {
        {
            auto w = composed::make_work_guard(exec);
            CHECK(exec.work_started == 1);
            CHECK(exec.work_finished == 0);

            {
                composed::work_guard<test_work_guard> w2;
                CHECK(exec.work_started == 1);
                CHECK(exec.work_finished == 0);

                w2 = std::move(w);
                CHECK(exec.work_started == 1);
                CHECK(exec.work_finished == 0);
            }
            CHECK(exec.work_started == 1);
            CHECK(exec.work_finished == 1);
        }
        CHECK(exec.work_started == 1);
        CHECK(exec.work_finished == 1);
    }

    SUBCASE("can be move-assigned to moved-from work") {
        {
            auto w = composed::make_work_guard(exec);
            CHECK(exec.work_started == 1);
            CHECK(exec.work_finished == 0);

            {
                composed::work_guard<test_work_guard> w2{std::move(w)};
                CHECK(exec.work_started == 1);
                CHECK(exec.work_finished == 0);

                w = std::move(w2);
                CHECK(exec.work_started == 1);
                CHECK(exec.work_finished == 0);
            }
            CHECK(exec.work_started == 1);
            CHECK(exec.work_finished == 0);
        }
        CHECK(exec.work_started == 1);
        CHECK(exec.work_finished == 1);
    }
}

#include <boost/asio/unyield.hpp>
