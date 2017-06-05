// Copyright (c) 2017 Barobo, Inc.
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

// Utilities for cancelling operations upon timeouts.

#ifndef COMPOSED_TIMED_HPP
#define COMPOSED_TIMED_HPP

#include <composed/associated_logger.hpp>

#include <beast/core/handler_ptr.hpp>

#include <boost/asio/steady_timer.hpp>
#include <boost/asio/async_result.hpp>
#include <boost/asio/handler_type.hpp>

#include <boost/assert.hpp>

#include <utility>
#include <tuple>

namespace composed {

template <class IoObject, class Token>
auto timed(IoObject& io_object, const boost::asio::steady_timer::time_point& tp, Token&& token);
// Create an Asio CompletionToken suitable to pass to any Asio asynchronous operation. The returned
// token behaves in all respects like the underlying wrapped token, except:
//   - if the operation DOES NOT complete by the absolute time point `tp`, `io_object.cancel()` is 
//     called and the operation is completed with `boost::asio::error::timed_out`.
//
// Example:
//
//   AsyncReadStream s;
//   auto tp = boost::asio::steady_timer::clock_type::now() + 1s;
//   s.async_read(buf, timed(s, tp, use_future)).get();
//
// That is, attempt to read from stream `s` for one second. If the read has not completed within
// that second, cancel all outstanding operations on `s`, and complete the read operation with a
// `timed_out` error.
//
// This has the effect of turning any asynchronous operation the returned token is passed to into a
// concurrent composed operation: the underlying operation and the timed wait operation happen
// at the same time. The timed operation as a whole will not complete until both the underlying
// operation and the wait operation have both completed (or been cancelled).
//
// There are three important caveats to this utility:
//   1. The IoObject whose operation is being timed MUST be protected by an explicit strand, or an
//      implicit one by dint of calling `io_service::run()` from only one thread at a time. This is
//      required because of the concurrent nature of the composed operation that the timed token
//      creates: it has no single chain of asynchronous operations, but a DAG of operations.
//
//   2. ALL outstanding operations on the IoObject will be cancelled, not just the operation to
//      which the timed token is passed. Asio does provide an interface offering finer-grained
//      control. For example, if you have a timed read op concurrent with a write op, you might end
//      up with the write op completiong with `operation_aborted` and the read op completing with
//      `timed_out`.
//
//   3. Passing a timed token to an operation introduces a small amount of overhead, equivalent to
//      `post()`ing the completion handler an extra time. This is necessary to ensure that the timer
//      object in the implementation is cleaned up and deallocated before final completion. On
//      high-level operations this overhead will be negligible. On low-level, low-latency
//      operations, this overhead could end up being noticable. In general, prefer to time high-
//      level operations.

template <class IoObject, class Token>
auto timed(IoObject& io_object, const boost::asio::steady_timer::duration& dur, Token&& token);
// Equivalent to `timed(io_object, boost::asio::steady_timer::clock_type::now() + dur, token)`.



// =======================================================================================
// Inline implementation

template <class IoObject, class Token>
struct timed_token {
    template <class DeducedToken>
    timed_token(IoObject& obj, const boost::asio::steady_timer::time_point& tp, DeducedToken&& t)
        : io_object(obj)
        , expiry(tp)
        , original(std::forward<DeducedToken>(t))
    {}

    IoObject& io_object;
    boost::asio::steady_timer::time_point expiry;
    Token original;
};

template <class IoObject, class Token>
auto timed(IoObject& io_object, const boost::asio::steady_timer::time_point& tp, Token&& token) {
    return timed_token<IoObject, Token>{io_object, tp, std::forward<Token>(token)};
}

template <class IoObject, class Token>
auto timed(IoObject& io_object, const boost::asio::steady_timer::duration& dur, Token&& token) {
    auto tp = boost::asio::steady_timer::clock_type::now() + dur;
    return timed(io_object, tp, std::forward<Token>(token));
}

template <class IoObject, class Signature>
struct timed_data;

template <class IoObject, class... Ts>
struct timed_data<IoObject, void(boost::system::error_code, Ts...)> {
    template <class DeducedHandler>
    timed_data(DeducedHandler&& handler, IoObject& obj)
        : lg(get_associated_logger(handler))
        , io_object(obj)
        , timer(io_object.get_io_service())
    {}

    logger lg;
    IoObject& io_object;
    boost::asio::steady_timer timer;
    std::tuple<boost::system::error_code, Ts...> result;
    bool done = false;
};

template <class IoObject, class Token, class Signature>
class timed_wait_handler {
public:
    using handler_type = typename boost::asio::handler_type<Token, Signature>::type;

    explicit timed_wait_handler(beast::handler_ptr<timed_data<IoObject, Signature>, handler_type> data)
        : d(std::move(data))
    {}

    void operator()(const boost::system::error_code& ec) {
        if (!d->done) {
            d->done = true;
            BOOST_ASSERT(!ec);
            std::get<0>(d->result) = boost::asio::error::timed_out;
            d->io_object.cancel();
        }
        else {
            invoke();
        }
    }

    void invoke () {
        auto r = std::move(d->result);
        auto f = [this](auto&&... args) { d.invoke(std::forward<decltype(args)>(args)...); };
        apply(f, std::move(r));
    }

    handler_type& original () const { return d.handler(); }

    friend void* asio_handler_allocate (size_t size, timed_wait_handler* self) {
        using boost::asio::asio_handler_allocate;
        return asio_handler_allocate(size, &self->original());
    }

    friend void asio_handler_deallocate (void* pointer, size_t size, timed_wait_handler* self) {
        using boost::asio::asio_handler_deallocate;
        asio_handler_deallocate(pointer, size, &self->original());
    }

    template <class Function>
    friend void asio_handler_invoke (Function&& f, timed_wait_handler* self) {
        using boost::asio::asio_handler_invoke;
        asio_handler_invoke(std::forward<Function>(f), &self->original());
    }

    friend bool asio_handler_is_continuation (timed_wait_handler* self) {
        using boost::asio::asio_handler_is_continuation;
        return asio_handler_is_continuation(&self->original());
    }

    using logger_type = associated_logger_t<handler_type>;
    logger_type get_logger() const { return get_associated_logger(original()); }

private:
    beast::handler_ptr<timed_data<IoObject, Signature>, handler_type> d;
};

template <class IoObject, class Token, class Signature>
class timed_handler {
public:
    using handler_type = typename boost::asio::handler_type<Token, Signature>::type;

    timed_handler(timed_token<IoObject, Token> token)
        : d(beast::handler_ptr<timed_data<IoObject, Signature>, handler_type>(
            handler_type{std::move(token.original)},
            token.io_object))
    {
        d->timer.expires_at(token.expiry);
        d->timer.async_wait(timed_wait_handler<IoObject, Token, Signature>{d});
    }

    template <class... Args>
    void operator()(const boost::system::error_code& ec, Args&&... args) {
        if (!d->done) {
            d->done = true;
            d->result = std::forward_as_tuple(ec, std::forward<Args>(args)...);
            d->timer.cancel();
        }
        else {
            if (ec == boost::asio::error::operation_aborted) {
                invoke();
            }
            else {
                d.invoke(ec, std::forward<Args>(args)...);
            }
        }
    }

    void invoke () {
        auto r = std::move(d->result);
        auto f = [this](auto&&... args) { d.invoke(std::forward<decltype(args)>(args)...); };
        apply(f, std::move(r));
    }

    handler_type& original () const { return d.handler(); }

    friend void* asio_handler_allocate (size_t size, timed_handler* self) {
        using boost::asio::asio_handler_allocate;
        return asio_handler_allocate(size, &self->original());
    }

    friend void asio_handler_deallocate (void* pointer, size_t size, timed_handler* self) {
        using boost::asio::asio_handler_deallocate;
        asio_handler_deallocate(pointer, size, &self->original());
    }

    template <class Function>
    friend void asio_handler_invoke (Function&& f, timed_handler* self) {
        using boost::asio::asio_handler_invoke;
        asio_handler_invoke(std::forward<Function>(f), &self->original());
    }

    friend bool asio_handler_is_continuation (timed_handler* self) {
        using boost::asio::asio_handler_is_continuation;
        return asio_handler_is_continuation(&self->original());
    }

    using logger_type = associated_logger_t<handler_type>;
    logger_type get_logger() const { return get_associated_logger(original()); }

private:
    beast::handler_ptr<timed_data<IoObject, Signature>, handler_type> d;
};

}  // composed

namespace boost { namespace asio {

template <class IoObject, class Token, class Signature>
struct async_result<::composed::timed_handler<IoObject, Token, Signature>> {
    using handler_type = typename ::composed::timed_handler<IoObject, Token, Signature>::handler_type;

public:
    using type = typename async_result<handler_type>::type;
    async_result(::composed::timed_handler<IoObject, Token, Signature>& handler)
        : result(handler.original())
    {}

    type get() { return result.get(); }

private:
    async_result<handler_type> result;
};

template <class IoObject, class Token, class Signature>
struct handler_type<::composed::timed_token<IoObject, Token>, Signature> {
    using type = ::composed::timed_handler<IoObject, Token, Signature>;
};

}} // boost::asio

#endif
