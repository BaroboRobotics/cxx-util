#ifndef UTIL_ASIO_COMPOSED_HPP
#define UTIL_ASIO_COMPOSED_HPP

#include <util/asio/asio_handler_hooks.hpp>
#include <util/applytuple.hpp>

#include <boost/asio/coroutine.hpp>
#include <boost/smart_ptr/intrusive_ptr.hpp>

#include <boost/system/error_code.hpp>

#include <new>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <utility>

#include <cassert>

namespace util {
namespace composed {

template <class Base, class Handler>
class OperationState : public Base {
public:
    template <class H, class... Args>
    explicit OperationState (H&& h, Args&&... args)
        : Base(std::forward<Args>(args)...)
        , handler_(std::forward<H>(h))
    {}

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
            asio_handler_hooks::deallocate(self, sizeof(OperationState), h);

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
    auto vp = asio_handler_hooks::allocate(sizeof(State), h);
    try {
        return new (vp) State(std::forward<Handler>(h), std::forward<Args>(args)...);
    }
    catch (...) {
        asio_handler_hooks::deallocate(vp, sizeof(State), h);
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
        return asio_handler_hooks::allocate(size, self->m->handler());
    }

    friend void asio_handler_deallocate (void* pointer, size_t size, Operation* self) {
        asio_handler_hooks::deallocate(pointer, size, self->m->handler());
    }

    template <class Function>
    friend void asio_handler_invoke (Function&& f, Operation* self) {
        asio_handler_hooks::invoke(std::forward<Function>(f), self->m->handler());
    }

    friend bool asio_handler_is_continuation (Operation* self) {
        return asio_handler_hooks::is_continuation(self->m->handler());
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

} // namespace composed
} // namespace util

#endif
