#include <util/doctest.h>
#include <util/asio/asynccompletion.hpp>
#include <util/asio/op.hpp>
#include <util/log.hpp>

#include <boost/asio.hpp>
#include <boost/asio/steady_timer.hpp>

#include <chrono>
#include <tuple>

#include <boost/asio/yield.hpp>

struct TestHandler {
    // A final handler to aid in checking all the handler hooks are being exercised correctly.

    const bool cont;

    size_t allocations = 0;
    size_t deallocations = 0;
    std::map<void*, size_t> allocationTable;

    static thread_local bool invokedOnMyContext;

    void operator()(boost::system::error_code ec, int count) {
        CHECK(allocations > 0);
        // There was at least one allocation.

        CHECK(allocationTable.empty());
        CHECK(deallocations == allocations);
        // But they were all deallocated before the handler was invoked.

        util::log::Logger lg;
        BOOST_LOG(lg) << "TestHandler: " << count << " (" << ec.message() << ")";
    }

    friend void* asio_handler_allocate(size_t size, TestHandler* self) {
        // Forward to the default implementation of asio_handler_allocate.
        int dummy;
        auto p = util::asio::handler_hooks::allocate(size, dummy);

        bool inserted;
        std::tie(std::ignore, inserted) = self->allocationTable.insert(std::make_pair(p, size));
        CHECK(inserted);
        // This allocation is new.

        ++self->allocations;

        auto lg = util::asio::getAssociatedLogger(*self);
        BOOST_LOG(lg) << "allocated " << p << " (" << size << " bytes)";

        return p;
    }

    friend void asio_handler_deallocate(void* pointer, size_t size, TestHandler* self) {
        auto lg = util::asio::getAssociatedLogger(*self);
        BOOST_LOG(lg) << "deallocating " << pointer << " (" << size << " bytes)";

        auto it = self->allocationTable.find(pointer);
        REQUIRE(it != self->allocationTable.end());
        // This allocation exists.

        CHECK(size == it->second);
        // And it is the stipulated size.

        self->allocationTable.erase(it);
        ++self->deallocations;

        // Forward to the default implementation of asio_handler_deallocate.
        int dummy;
        util::asio::handler_hooks::deallocate(pointer, size, dummy);
    }

    template <class Function>
    friend void asio_handler_invoke(Function&& function, TestHandler* self) {
        BOOST_SCOPE_EXIT_ALL(&) {
            TestHandler::invokedOnMyContext = false;
        };
        TestHandler::invokedOnMyContext = true;
        // Composed operations can check this thread_local to ensure that their continuations were
        // invoked through this function.

        // Forward to the default implementation of asio_handler_invoke.
        int dummy;
        util::asio::handler_hooks::invoke(std::forward<Function>(function), dummy);
    }

    friend bool asio_handler_is_continuation(TestHandler* self) {
        return self->cont;
    }
};

thread_local bool TestHandler::invokedOnMyContext = false;

// =======================================================================================
// A struct-based composed operation implementation

template <class Handler>
struct TestOp {
    using HandlerType = Handler;
    using allocator_type = std::scoped_allocator_adaptor<beast::handler_alloc<char, Handler>>;
    // or just:
    //using allocator_type = beast::handler_alloc<char, Handler>;

    boost::asio::steady_timer& timer;
    boost::asio::coroutine coro;
    boost::system::error_code ec;
    int count = 0;
    constexpr static int kMaxCount = 10;

    std::vector<int, allocator_type> v;

    TestOp(boost::asio::steady_timer& t, allocator_type alloc): timer(t), v(alloc) {}

    void operator()(util::asio::Op<TestOp>& op);
};

template <class Handler>
void TestOp<Handler>::operator()(util::asio::Op<TestOp>& op) {
    auto role = boost::log::attribute_cast<boost::log::attributes::constant<std::string>>(
            op.log().get_attributes()["Role"]);
    CHECK(role.get() == "struct");

    BOOST_LOG(op.log()) << "Task: " << count;
    if (!ec) reenter (coro) {
        while (++count < kMaxCount) {
            timer.expires_from_now(std::chrono::milliseconds(100));
            yield timer.async_wait(op(ec));
            CHECK(TestHandler::invokedOnMyContext);
            v.insert(v.end(), 1024, count);
        }
        op.complete(ec, count);
    }
    else {
        op.complete(ec, count);
    }
}

template <class Token>
auto asyncTestStruct(boost::asio::steady_timer& timer, Token&& token) {
    util::asio::AsyncCompletion<Token, void()> init { std::forward<Token>(token) };

    util::asio::startOp<TestOp<decltype(init.handler)>>(timer, std::move(init.handler));

    return init.result.get();
}

// =======================================================================================
// A lambda-based composed operation implementation

template <class Token>
auto asyncTestLambda(boost::asio::steady_timer& timer, Token&& token) {
    util::asio::AsyncCompletion<Token, void()> init { std::forward<Token>(token) };

    util::asio::startSimpleOp(
    [ &timer
    , coro = boost::asio::coroutine{}
    , ec = boost::system::error_code{}
    , count = 0
    ](auto& op) mutable {
        constexpr int kMaxCount = 10;
        auto role = boost::log::attribute_cast<boost::log::attributes::constant<std::string>>(
                op.log().get_attributes()["Role"]);
        CHECK(role.get() == "lambda");

        BOOST_LOG(op.log()) << "Task: " << count;
        if (!ec) reenter (coro) {
            while (++count < kMaxCount) {
                timer.expires_from_now(std::chrono::milliseconds(100));
                yield timer.async_wait(op(ec));
                CHECK(TestHandler::invokedOnMyContext);
            }
            op.complete(ec, count);
        }
        else {
            op.complete(ec, count);
        }
    }, std::move(init.handler));

    return init.result.get();
}

// =======================================================================================
// Test cases

TEST_CASE("can start an Op") {
    boost::asio::io_service context;
    boost::asio::steady_timer timer{context};

    auto token = util::asio::addAssociatedLogger(TestHandler{false}, util::log::Logger{});

    SUBCASE("with a struct Task") {
        token.log().add_attribute("Role", boost::log::attributes::constant<std::string>("struct"));
        asyncTestStruct(timer, std::move(token));
        context.run();
    }

    SUBCASE("with a lambda Task") {
        token.log().add_attribute("Role", boost::log::attributes::constant<std::string>("lambda"));
        asyncTestLambda(timer, std::move(token));
        context.run();
    }
}

#include <boost/asio/unyield.hpp>











#if 0
// Just an idea

template <class Token = composed::token>
struct test_op: composed::operation<Token, void(error_coode)> {
    using allocator_type = typename test_op::allocator_type;

    boost::asio::steady_timer& timer;
    boost::system::error_code ec;

    std::vector<int, allocator_type> v;

    test_op(boost::asio::steady_timer& timer, allocator_type alloc): v(alloc) {}

    auto operator()(composed::op<test_op>& op) {
        if (!ec) COMPOSED_REENTER (this) {
            timer.expires_from_now(std::chrono::milliseconds(100));
            COMPOSED_YIELD return timer.async_wait(op(ec));
        }
        return op.complete(ec);
    }
};

template <class Token>
auto async_test(boost::asio::steady_timer& timer, Token&& token) {
    composed::async_completion<Token, void(error_code)> init{std::forward<Token>(token)};
    composed::start_op<test_op<decltype(init.handler)>>(timer, std::move(init.handler));
    return init.result.get();
}
    // or
template <class... Args>
auto async_test(Args&&... args) {
    return composed::async_run<test_op<>>(std::forward<Args>(args)...);
}

#endif