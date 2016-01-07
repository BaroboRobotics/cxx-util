#ifndef UTIL_COMPOSEDOPERATION_HPP
#define UTIL_COMPOSEDOPERATION_HPP

#include <util/asio_handler_hooks.hpp>
#include <util/applytuple.hpp>

#include <boost/asio/coroutine.hpp>
#include <boost/smart_ptr/intrusive_ptr.hpp>
#include <boost/smart_ptr/intrusive_ref_counter.hpp>

#include <boost/system/error_code.hpp>

#include <new>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <utility>

#include <cassert>

namespace util {

namespace detail {

template <class StateBase, class Handler>
struct ComposedOpState : StateBase {
    template <class S, class H>
    ComposedOpState (S&& s, H&& h)
        : StateBase(std::forward<S>(s))
        , handler(std::forward<H>(h))
    {}

    Handler handler;
    size_t refs = 0;

    template <class S, class H>
    static boost::intrusive_ptr<ComposedOpState> make (S&& s, H&& h) {
        auto vp = asio_handler_hooks::allocate(sizeof(ComposedOpState), h);
        try {
            return new (vp) ComposedOpState{std::forward<S>(s), std::forward<H>(h)};
        }
        catch (...) {
            asio_handler_hooks::deallocate(vp, sizeof(ComposedOpState), h);
            throw;
        }
    }

    friend void intrusive_ptr_add_ref (ComposedOpState* self) {
        assert(self);
        ++self->refs;
    }

    friend void intrusive_ptr_release (ComposedOpState* self) {
        assert(self);
        if (!--self->refs) {
            static_assert(std::is_nothrow_move_constructible<Handler>::value,
                "Handler's move constructor must be noexcept");
            //static_assert(noexcept(auto r = self->result()), "Operation's result() function must be noexcept");
            // Remove the operation's completion handler and result
            auto h = std::move(self->handler);
            auto result = self->result();

            // Release the operation state
            self->~ComposedOpState();
            asio_handler_hooks::deallocate(self, sizeof(ComposedOpState), h);

            // Call the operation's completion handler
            applyTuple(std::move(h), std::move(result));
        }
    }
};

}

template <class StateBase, class Handler>
class ComposedOp {
    using State = detail::ComposedOpState<StateBase, Handler>;

public:
    template <class S, class H>
    ComposedOp (S&& s, H&& h)
        : m(State::make(std::forward<S>(s), std::forward<H>(h)))
    {}

    ComposedOp (const ComposedOp&) = default;
    ComposedOp (ComposedOp&& other)
        : m(std::move(other.m))
        , coro(other.coro)
    {}

    ComposedOp& operator= (const ComposedOp&) = delete;
    ComposedOp& operator= (ComposedOp&&) = delete;

    operator boost::asio::coroutine& () {
        return coro;
    }

    template <class... Args>
    void operator() (Args&&... args) {
        assert(m);
        (*m)(std::move(*this), std::forward<Args>(args)...);
        // *this may be moved-from at this point! But don't panic: coro is
        // guaranteed by the move ctor to be in a valid state, and m will be a
        // nullptr, so reset() will be a no-op. In all likelihood, the reason
        // for the move will have been to pass *this on to an asynchronous
        // function, in which case a yield will have been issued, so
        // is_complete() will return false anyway.
        // Note: the only reason this is_complete()/reset() business is here is
        // so that Asio's handler tracking system can properly identify
        // m->handler as a child of this operation. This is only useful for
        // debugging--if we left the destructor to perform the reset(), and
        // thus post the completion handler, all would still work. 
        if (coro.is_complete()) {
            m.reset();
        }
    }

    // Inherit the allocation, invocation, and continuation strategies from the
    // operation's completion handler.
    friend void* asio_handler_allocate (size_t size, ComposedOp* self) {
        return asio_handler_hooks::allocate(size, self->m->handler);
    }

    friend void asio_handler_deallocate (void* pointer, size_t size, ComposedOp* self) {
        asio_handler_hooks::deallocate(pointer, size, self->m->handler);
    }

    template <class Function>
    friend void asio_handler_invoke (Function&& f, ComposedOp* self) {
        asio_handler_hooks::invoke(std::forward<Function>(f), self->m->handler);
    }

    friend bool asio_handler_is_continuation (ComposedOp* self) {
        return asio_handler_hooks::is_continuation(self->m->handler)
    }

private:
    boost::intrusive_ptr<State> m;
    boost::asio::coroutine coro;
};

// Convenience function to construct a ComposedOp.
template <class StateBase, class Handler>
ComposedOp<
    typename std::decay<StateBase>::type,
    typename std::decay<Handler>::type
    >
makeComposedOp (StateBase&& s, Handler&& h) {
    return {std::forward<StateBase>(s), std::forward<Handler>(h)};
}

} // namespace util

#endif