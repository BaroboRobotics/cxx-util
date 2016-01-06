#ifndef UTIL_COMPOSEDOPERATION_HPP
#define UTIL_COMPOSEDOPERATION_HPP

#include <util/asio_handler_hooks.hpp>
#include <util/applytuple.hpp>

#include <boost/asio/coroutine.hpp>

#include <new>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <utility>

#include <cassert>

namespace util {

namespace detail {

template <class Signature>
struct ComposedOpResultImpl;

template <class... Args>
struct ComposedOpResultImpl<void(Args...)> {
    using type = std::tuple<typename std::decay<Args>::type...>;
};

} // namespace detail

template <class Signature>
using ComposedOpResult = typename detail::ComposedOpResultImpl<Signature>::type;

namespace detail {

// MSVC chokes on using enable_if to mutually exclusively enable these two
// functions as ComposedOp::operator(), so use template specialization to make
// it work instead.
template <class Branch>
struct CallWithBranch {
    template <class F, class Op, class... Args>
    static void call (F&& f, Op&& op, Args&&... args) {
        std::forward<F>(f)(std::forward<Op>(op), Branch{}, std::forward<Args>(args)...);
    }
};

template <>
struct CallWithBranch<void> {
    template <class F, class Op, class... Args>
    static void call (F&& f, Op&& op, Args&&... args) {
        std::forward<F>(f)(std::forward<Op>(op), std::forward<Args>(args)...);
    }
};

}

template <class StateBase, class Handler, class Signature, class Branch = void>
class ComposedOp;

template <class StateBase, class Handler, class... SigArgs, class Branch>
class ComposedOp<StateBase, Handler, void(SigArgs...), Branch> {
    typedef void Signature(SigArgs...);

public:
    template <class S, class H>
    ComposedOp (S&& s, H&& h)
    {
        auto v = asio_handler_hooks::allocate(sizeof(State), h);
        try {
            m = new (v) State{std::forward<S>(s), std::forward<H>(h)};
            ++m->refs;
        }
        catch (...) {
            asio_handler_hooks::deallocate(v, sizeof(State), h);
            throw;
        }
    }

    ComposedOp (const ComposedOp& other)
        : m(other.m)
        , coro(other.coro)
    {
        if (m) {
            ++m->refs;
        }
    }

    ComposedOp (ComposedOp&& other)
        : m(other.m)
        , coro(other.coro)
    {
        // other no longer owns the state memory
        other.m = nullptr;
        // Leave other.coro alone. If the op is being moved from inside a
        // reenter block, resetting other.coro to its default will prevent the
        // moved-from coroutine from bailing out, and the Duff's Device will go
        // into an infinite loop. Learned the hard way.
    }

#if 0
    ComposedOp& operator= (ComposedOp other) {
        using std::swap;
        swap(*this, other);
        return *this;
    }
#endif

    ~ComposedOp () {
        // If this op was copied, useCount() must be > 1.
        // If this op was moved, useCount() must be 0, because we no longer own m.
        // If this op was neither moved nor copied, the operation must have
        // completed, in which case reset() must already have been called after
        // the last call to operator().
        assert(useCount() != 1);
        reset();
    }

#if 0
    template <class OtherBranch>
    ComposedOp (const ComposedOp<StateBase, Handler, Signature, OtherBranch>& other)
        : m(other.m)
    {
        if (m) {
            ++m->refs;
        }
    }

    template <class OtherBranch>
    ComposedOp (ComposedOp<StateBase, Handler, Signature, OtherBranch>&& other) {
        using std::swap;
        swap(*this, other);
    }

    template <class OtherBranch>
    ComposedOp<StateBase, Handler, Signature, OtherBranch> branch () const {
        return {*this};
    }
#endif

    operator boost::asio::coroutine& () {
        return coro;
    }

    template <class... Args>
    void operator() (Args&&... args) {
        assert(m);
        detail::CallWithBranch<Branch>::call(*m, std::move(*this), std::forward<Args>(args)...);
        // *this may be moved-from at this point! But don't panic: coro is
        // guaranteed by the move ctor to be in a valid state, and m will be a
        // nullptr, so reset() will be a no-op. In all likelihood, the reason
        // for the move will have been to pass *this on to an asynchronous
        // function, in which case a yield will have been issued, so
        // is_complete() will return false anyway.
        if (coro.is_complete()) {
            reset();
        }
    }

    size_t useCount () const {
        return m ? m->refs : 0;
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
    void reset () {
        if (m) {
            if (!--m->refs) {
                static_assert(std::is_nothrow_move_constructible<Handler>::value,
                    "Handler's move constructor must be noexcept");
                //static_assert(noexcept(auto r = m->result()), "Operation's result() function must be noexcept");
                auto h = std::move(m->handler);
                auto result = m->result();

                m->~State();
                asio_handler_hooks::deallocate(m, sizeof(State), h);
                m = nullptr;

                applyTuple(std::move(h), std::move(result));
            }
            else {
                m = nullptr;
            }
        }
    }

    struct State : StateBase {
        template <class S, class H>
        State (S&& s, H&& h)
            : StateBase(std::forward<S>(s))
            , handler(std::forward<H>(h))
        {}
        size_t refs = 0;
        Handler handler;
    };

    State* m = nullptr;
    boost::asio::coroutine coro;
};

// Convenience function to construct a ComposedOp.
template <class Signature, class StateBase, class Handler>
ComposedOp<
    typename std::decay<StateBase>::type,
    typename std::decay<Handler>::type,
    Signature>
makeComposedOp (StateBase&& s, Handler&& h) {
    return {std::forward<StateBase>(s), std::forward<Handler>(h)};
}

} // namespace util

#endif