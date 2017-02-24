// Copyright (c) 2017 Barobo, Inc.
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

// Utilities for cancelling operations upon receiving OS signals.
//
// FIXME: code duplication with timed.hpp.

#ifndef COMPOSED_SIGNALLED_HPP
#define COMPOSED_SIGNALLED_HPP

#include <composed/associated_logger.hpp>

#include <beast/core/handler_ptr.hpp>

#include <boost/asio/signal_set.hpp>
#include <boost/asio/async_result.hpp>
#include <boost/asio/handler_type.hpp>

#include <boost/assert.hpp>

#include <utility>
#include <tuple>

namespace composed {

template <class IoObject, size_t N, class Token>
auto signalled(IoObject& io_object, const std::array<int, N>& sigs, Token&& token);
// Create an Asio CompletionToken suitable to pass to any Asio asynchronous operation. The returned
// token behaves in all respects like the underlying wrapped token, except:
//   - if the process receives any signal in `sigs`, `io_object.cancel()` is called and the
//     operation is completed with `boost::asio::error::interrupted`.
//
// Beyond the triggering criterion (signal versus timeout) and the error_code used, a signalled
// token behaves exactly like a timed token. See timed.hpp for details.



// =======================================================================================
// Inline implementation

template <class IoObject, size_t N, class Token>
struct signalled_token {
    template <class DeducedToken>
    signalled_token(IoObject& obj, const std::array<int, N>& sigs, DeducedToken&& t)
        : io_object(obj)
        , signals(sigs)
        , original(std::forward<DeducedToken>(t))
    {}

    IoObject& io_object;
    std::array<int, N> signals;
    Token original;
};

template <class IoObject, size_t N, class Token>
auto signalled(IoObject& io_object, const std::array<int, N>& sigs, Token&& token) {
    return signalled_token<IoObject, N, Token>{io_object, sigs, std::forward<Token>(token)};
}

template <class IoObject, class Signature>
struct signalled_data;

template <class IoObject, class... Ts>
struct signalled_data<IoObject, void(boost::system::error_code, Ts...)> {
    template <class DeducedHandler>
    signalled_data(DeducedHandler&& handler, IoObject& obj)
        : lg(get_associated_logger(handler))
        , io_object(obj)
        , sigset(io_object.get_io_service())
    {}

    logger lg;
    IoObject& io_object;
    boost::asio::signal_set sigset;
    std::tuple<boost::system::error_code, Ts...> result;
    bool done = false;
};

template <class IoObject, class Token, class Signature>
class signalled_wait_handler {
public:
    using handler_type = typename boost::asio::handler_type<Token, Signature>::type;

    explicit signalled_wait_handler(beast::handler_ptr<signalled_data<IoObject, Signature>, handler_type> data)
        : d(std::move(data))
    {}

    void operator()(const boost::system::error_code& ec, int sig_no) {
        if (!d->done) {
            d->done = true;
            BOOST_ASSERT(!ec);
            BOOST_LOG(d->lg) << "Received signal " << sig_no;
            std::get<0>(d->result) = boost::asio::error::interrupted;
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

    friend void* asio_handler_allocate (size_t size, signalled_wait_handler* self) {
        return beast_asio_helpers::allocate(size, self->original());
    }

    friend void asio_handler_deallocate (void* pointer, size_t size, signalled_wait_handler* self) {
        beast_asio_helpers::deallocate(pointer, size, self->original());
    }

    template <class Function>
    friend void asio_handler_invoke (Function&& f, signalled_wait_handler* self) {
        beast_asio_helpers::invoke(std::forward<Function>(f), self->original());
    }

    friend bool asio_handler_is_continuation (signalled_wait_handler* self) {
        return beast_asio_helpers::is_continuation(self->original());
    }

    using logger_type = associated_logger_t<handler_type>;
    logger_type get_logger() const { return get_associated_logger(original()); }

private:
    beast::handler_ptr<signalled_data<IoObject, Signature>, handler_type> d;
};

template <class IoObject, class Token, class Signature>
class signalled_handler {
public:
    using handler_type = typename boost::asio::handler_type<Token, Signature>::type;

    template <size_t N>
    signalled_handler(signalled_token<IoObject, N, Token> token)
        : d(beast::handler_ptr<signalled_data<IoObject, Signature>, handler_type>(
            handler_type{std::move(token.original)},
            token.io_object))
    {
        for (auto sig: token.signals) {
            d->sigset.add(sig);
        }
        d->sigset.async_wait(signalled_wait_handler<IoObject, Token, Signature>{d});
    }

    template <class... Args>
    void operator()(const boost::system::error_code& ec, Args&&... args) {
        if (!d->done) {
            d->done = true;
            d->result = std::forward_as_tuple(ec, std::forward<Args>(args)...);
            d->sigset.cancel();
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

    friend void* asio_handler_allocate (size_t size, signalled_handler* self) {
        return beast_asio_helpers::allocate(size, self->original());
    }

    friend void asio_handler_deallocate (void* pointer, size_t size, signalled_handler* self) {
        beast_asio_helpers::deallocate(pointer, size, self->original());
    }

    template <class Function>
    friend void asio_handler_invoke (Function&& f, signalled_handler* self) {
        beast_asio_helpers::invoke(std::forward<Function>(f), self->original());
    }

    friend bool asio_handler_is_continuation (signalled_handler* self) {
        return beast_asio_helpers::is_continuation(self->original());
    }

    using logger_type = associated_logger_t<handler_type>;
    logger_type get_logger() const { return get_associated_logger(original()); }

private:
    beast::handler_ptr<signalled_data<IoObject, Signature>, handler_type> d;
};

}  // composed

namespace boost { namespace asio {

template <class IoObject, class Token, class Signature>
struct async_result<::composed::signalled_handler<IoObject, Token, Signature>> {
    using handler_type = typename ::composed::signalled_handler<IoObject, Token, Signature>::handler_type;

public:
    using type = typename async_result<handler_type>::type;
    async_result(::composed::signalled_handler<IoObject, Token, Signature>& handler)
        : result(handler.original())
    {}

    type get() { return result.get(); }

private:
    async_result<handler_type> result;
};

template <class IoObject, size_t N, class Token, class Signature>
struct handler_type<::composed::signalled_token<IoObject, N, Token>, Signature> {
    using type = ::composed::signalled_handler<IoObject, Token, Signature>;
};

}} // namespace boost::asio

#endif
