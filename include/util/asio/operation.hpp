// Copyright (c) 2014-2016 Barobo, Inc.
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#ifndef UTIL_ASIO_OPERATION_HPP
#define UTIL_ASIO_OPERATION_HPP

#include <util/log.hpp>
#include <util/asio/handler_hooks.hpp>
#include <util/asio/associatedlogger.hpp>
#include <util/asio/asynccompletion.hpp>
#include <util/applytuple.hpp>

#include <boost/asio/coroutine.hpp>
#include <boost/smart_ptr/intrusive_ptr.hpp>

#include <boost/system/error_code.hpp>

#include <boost/scope_exit.hpp>

#include <new>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <utility>

#include <cassert>

namespace util { namespace asio {

inline namespace v2 {

namespace _ {

template <class Coroutine, class Handler, class... Results>
class OperationState {
public:
    template <class C, class H>
    OperationState (std::tuple<Results...>&& defaultResult, C&& coroutine, H&& handler)
        : mResult(std::move(defaultResult))
        , mCoroutine(std::forward<C>(coroutine))
        , mHandler(std::forward<H>(handler))
    {}

    Handler& handler () {
        return mHandler;
    }

    template <class... Args>
    void operator() (Args&&... args) {
        mCoroutine(std::forward<Args>(args)...);
    }

    template <class... Rs>
    void complete (std::tuple<Rs...>&& result) {
        mResult = std::move(result);
    }

    friend void intrusive_ptr_add_ref (OperationState* self) {
        assert(self);
        ++self->mRefs;
    }

    friend void intrusive_ptr_release (OperationState* self) try {
        assert(self);
        if (!--self->mRefs) {
#if 0
            // This is a bit of a mess at the moment... for one thing, Boost.Log sources are not
            // nothrow move-constructible. :(
            static_assert(std::is_nothrow_move_constructible<Handler>::value,
                "Handler's move constructor must be noexcept");
            static_assert(std::is_nothrow_move_constructible<std::tuple<Results...>>::value,
                "Result's move constructor must be noexcept");
#endif
            // Remove the operation's completion handler and result
            auto h = std::move(self->handler());
            auto result = std::move(self->mResult);

            // Release the operation state
            self->~OperationState();
            handler_hooks::deallocate(self, sizeof(OperationState), h);

            // Call the operation's completion handler
            applyTuple(std::move(h), std::move(result));
        }
    }
    catch (const std::exception& e) {
        util::log::Logger lg;
        BOOST_LOG(lg) << "Exception thrown in intrusive_ptr_release! Prepare to crash. "
                << "Tell Harris to fix his bug. Exception: " << e.what();
        throw;
    }

private:
    std::tuple<Results...> mResult;
    Coroutine mCoroutine;
    Handler mHandler;
    size_t mRefs = 0;
};

template <class Coroutine, class Handler, class... Results>
class Operation : public boost::asio::coroutine {
public:
    using State = OperationState<Coroutine, Handler, Results...>;

    Operation() = delete;
    Operation (boost::intrusive_ptr<State> p) : m(std::move(p)) {}

    void runChild () {
        // Create a copy and immediately execute it. To fork a coroutine, use like so:
        //   fork op.runChild();
        //   if (op.is_child()) {
        //       // A
        //       return;
        //   }
        //   // B
        // In this code, A will run, then B will immediately follow.
        Operation{*this}();
    }

    template <class... Args>
    void operator() (Args&&... args) {
        assert(m);
        // An assertion failure here often means you forgot a yield or fork macro in
        // your coroutine, or are forking in an unsafe way (use `runChild()`).

        (*m)(std::move(*this), std::forward<Args>(args)...);
        // If our coroutine base class is complete, we don't need our state
        // pointer anymore. We could let the destructor take care of it, but
        // resetting it here is preferable so that Asio's handler tracking
        // system--useful for debugging--can properly track our asynchronous
        // call graph.
        // Note that if *this has been moved at this point, we're still in
        // good shape: the coroutine will almost certainly not be complete, and
        // even if it were, m is just a null pointer, so reset() is a no-op.
        if (is_complete()) {
            m.reset();
        }
    }

    template <class... Rs>
    void complete (Rs&&... results) {
        m->complete(std::forward_as_tuple(results...));
    }

    log::Logger& log () const {
        return ::util::asio::getAssociatedLogger(m->handler());
    }

    friend log::Logger& getAssociatedLogger (const Operation& op) {
        return op.log();
    }

    // Inherit the allocation, invocation, and continuation strategies from the
    // operation's completion handler.
    friend void* asio_handler_allocate (size_t size, Operation* self) {
        return handler_hooks::allocate(size, self->m->handler());
    }

    friend void asio_handler_deallocate (void* pointer, size_t size, Operation* self) {
        handler_hooks::deallocate(pointer, size, self->m->handler());
    }

    template <class Function>
    friend void asio_handler_invoke (Function&& f, Operation* self) {
        handler_hooks::invoke(std::forward<Function>(f), self->m->handler());
    }

    friend bool asio_handler_is_continuation (Operation* self) {
        return handler_hooks::is_continuation(self->m->handler());
    }

private:
    boost::intrusive_ptr<State> m;
};

} // _

// Convenience function to construct an Operation.
template <class Coroutine, class Handler, class... Results>
_::Operation<typename std::decay<Coroutine>::type, typename std::decay<Handler>::type, Results...>
makeOperation (std::tuple<Results...>&& defaultResult, Coroutine&& c, Handler&& h) {
    using State = _::OperationState<
        typename std::decay<Coroutine>::type, typename std::decay<Handler>::type, Results...>;

    auto vp = handler_hooks::allocate(sizeof(State), h);
    State* p = nullptr;

    BOOST_SCOPE_EXIT_ALL(&) {
        if (!p) {
            handler_hooks::deallocate(vp, sizeof(State), h);
        }
    };

    p = new (vp) State(
            std::move(defaultResult), std::forward<Coroutine>(c), std::forward<Handler>(h));
    // Handler is the last value constructed in State, so the ScopeExit guard should have a valid
    // handler to deallocate with in case State's ctor throws.

    return boost::intrusive_ptr<State>(p);
}

template <class Context, class Coroutine, class CompletionToken, class... Results>
auto asyncDispatch (Context& context, std::tuple<Results...>&& defaultResult,
        Coroutine&& coroutine, CompletionToken&& token) {
    util::asio::AsyncCompletion<
        CompletionToken, void(Results...)
    > init { std::forward<CompletionToken>(token) };

    auto op = makeOperation(
        std::move(defaultResult), std::forward<Coroutine>(coroutine),
        std::move(init.handler));
    context.dispatch(std::move(op));

    return init.result.get();
}

} // v2

namespace v1 {

template <class Base, class Handler>
class OperationState : public Base {
public:
    template <class H, class... Args>
    explicit OperationState (H&& h, Args&&... args)
        : Base(std::forward<Args>(args)...)
        , handler_(std::forward<H>(h))
    {}

    OperationState(const OperationState&) = delete;
    OperationState(OperationState&&) = default;
    OperationState& operator=(const OperationState&) = delete;
    OperationState& operator=(OperationState&&) = default;
    // Movable, noncopyable

    ~OperationState() = default;

    Handler& handler () {
        return handler_;
    }

    friend void intrusive_ptr_add_ref (OperationState* self) {
        assert(self);
        ++self->refs_;
    }

    friend void intrusive_ptr_release (OperationState* self) {
        assert(self);
        if (!--self->refs_) {
#if 0
            static_assert(std::is_nothrow_move_constructible<Handler>::value,
                "Handler's move constructor must be noexcept");
            static_assert(noexcept(auto r = self->result()), "Operation's result() function must be noexcept");
#endif
            // Remove the operation's completion handler and result
            auto h = std::move(self->handler());
            auto result = self->result();

            // Release the operation state
            self->~OperationState();
            handler_hooks::deallocate(self, sizeof(OperationState), h);

            // Call the operation's completion handler
            applyTuple(std::move(h), std::move(result));
        }
    }

private:
    Handler handler_;
    size_t refs_ = 0;
};

template <class Base, class Handler, class... Args>
boost::intrusive_ptr<OperationState<Base, typename std::decay<Handler>::type>>
makeOperationState (Handler&& h, Args&&... args) {
    using State = OperationState<Base, typename std::decay<Handler>::type>;
    auto vp = handler_hooks::allocate(sizeof(State), h);
    try {
        return new (vp) State(std::forward<Handler>(h), std::forward<Args>(args)...);
    }
    catch (...) {
        handler_hooks::deallocate(vp, sizeof(State), h);
        throw;
    }
}

template <class Base, class Handler>
class Operation : public boost::asio::coroutine {
public:
    using State = OperationState<Base, Handler>;

    Operation (boost::intrusive_ptr<State>&& p) : m(p) {}

    Operation (const Operation&) = default;
    Operation (Operation&& other)
#if 0
        noexcept(std::is_nothrow_move_constructible<decltype(m)>::value
            && std::is_nothrow_copy_constructible<decltype(coro)>::value)
#endif
        : boost::asio::coroutine(other)
        , m(std::move(other.m))
    {}

    Operation& operator= (const Operation&) = delete;
    Operation& operator= (Operation&&) = delete;

    template <class... Args>
    void operator() (Args&&... args) {
        // An assertion failure here often means you forgot a yield macro in
        // your coroutine.
        assert(m);
        (*m)(std::move(*this), std::forward<Args>(args)...);
        // If our coroutine base class is complete, we don't need our state
        // pointer anymore. We could let the destructor take care of it, but
        // resetting it here is preferable so that Asio's handler tracking
        // system--useful for debugging--can properly track our asynchronous
        // call graph.
        // Note that *this may be moved-from at this point, but that just means
        // our state pointer will be null.
        if (is_complete()) {
            m.reset();
        }
    }

    // Inherit the allocation, invocation, and continuation strategies from the
    // operation's completion handler.
    friend void* asio_handler_allocate (size_t size, Operation* self) {
        return handler_hooks::allocate(size, self->m->handler());
    }

    friend void asio_handler_deallocate (void* pointer, size_t size, Operation* self) {
        handler_hooks::deallocate(pointer, size, self->m->handler());
    }

    template <class Function>
    friend void asio_handler_invoke (Function&& f, Operation* self) {
        handler_hooks::invoke(std::forward<Function>(f), self->m->handler());
    }

    friend bool asio_handler_is_continuation (Operation* self) {
        return handler_hooks::is_continuation(self->m->handler());
    }

private:
    boost::intrusive_ptr<State> m;
};

// Convenience function to construct an Operation.
template <class Base, class Handler, class... Args>
Operation<Base, typename std::decay<Handler>::type>
makeOperation (Handler&& h, Args&&... args) {
    return makeOperationState<Base>(std::forward<Handler>(h), std::forward<Args>(args)...);
}

} // v1

}} // namespace util::asio

#endif
