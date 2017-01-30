// Copyright (c) 2014-2017 Barobo, Inc.
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#ifndef COMPOSED_ASSOCIATED_LOGGER_HPP
#define COMPOSED_ASSOCIATED_LOGGER_HPP

#include <composed/stdlib.hpp>

#include <beast/core/handler_helpers.hpp>

#include <boost/log/attributes/constant.hpp>
#include <boost/log/keywords/channel.hpp>
#include <boost/log/sources/severity_channel_logger.hpp>
#include <boost/log/sources/record_ostream.hpp>

#include <boost/asio/io_service.hpp>
#include <boost/asio/async_result.hpp>

#include <utility>
#include <functional>

namespace composed {

class logger {
public:
    // Cheap copyable wrapper around a Boost.Log logging source, forwarding just enough of the API
    // to satisfy the BOOST_LOG macro requirements.
    //
    // This wrapper contains a raw pointer to the wrapped logging source, so that source's lifetime
    // MUST outlive this wrapper's lifetime.

    using logger_type = boost::log::sources::severity_channel_logger<>;

    // The three things that BOOST_LOG() needs are char_type, open_record, and push_record.

    using char_type = logger_type::char_type;

    boost::log::record open_record() { return lg->open_record(); }
    void push_record(boost::log::record&& r) { lg->push_record(std::move(r)); }

    template <class... Args>
    decltype(auto) add_attribute(Args&&... args) {
        return lg->add_attribute(std::forward<Args>(args)...);
    }
    decltype(auto) get_attributes() const { return lg->get_attributes(); }

    explicit logger(logger_type* l = default_logger()): lg(l) {}

    static logger_type* default_logger() {
        thread_local logger_type lg;
        return &lg;
    }

private:
    logger_type* lg;
};



// The following infrastructure follows the pattern set by the Networking TS for associated objects.
// See https://wg21.link/n4625 sections 13.2.6, 13.5, 13.6, 13.12, 13.13.
//
// The simplest way to opt into the associated_logger framework with your custom handler class is to
// provide an appropriate `logger_type` member typedef and `get_logger` member function like so:
//
//   struct my_handler {
//       using logger_type = ...;
//       logger_type get_logger() const { return ...; }
//       ...
//   };
//
// You can also specialize the `associated_logger` associator explicitly, which would allow you to
// define an associated logger for handlers whose code you cannot modify.

template <class T, class L = logger, class = void>
struct associated_logger {
    // Default associator. Uses L as the logger type and returns the passed default logger.

    using type = L;
    static type get(const T&, const L& l = L{}) { return l; }
};

template <class T, class L>
struct associated_logger<T, L, void_t<typename T::logger_type>> {
    // Associator providing the ability to easily opt by defining a `logger_type` member typedef and
    // a `get_logger` member function.

    using type = typename T::logger_type;
    static type get(const T& t, const L& = L{}) { return t.get_logger(); }
};

template <class T, class L = logger>
using associated_logger_t = typename associated_logger<T, L>::type;

template <class T>
associated_logger_t<T> get_associated_logger(const T& t) noexcept {
    return associated_logger<T>::get(t);
}

template <class T, class L>
associated_logger_t<T, L> get_associated_logger(const T& t, const L& l) noexcept {
    return associated_logger<T, L>::get(t, l);
}

}  // composed

#if 0
template <class Token>
class logger_binder {
    // A logger_binder attaches a logger object to a wrapped completion token. When the
    // logger_binder is converted to a handler object (e.g., through async_completion), the logger
    // is copied over to the handler object. Code with access to the handler can access this logger
    // with `get_associated_logger(handler)`.

public:
    template <class DeducedToken>
    logger_binder(DeducedToken&& t, const logger& l)
        : token(std::forward<DeducedToken>(t))
        , lg(l)
    {}

    using logger_type = logger;
    logger_type get_logger() const noexcept { return lg; }

    Token&& release_token() noexcept {
        static_assert(std::is_move_constructible<Token>::value, "Token must be move-constructible");
        return std::move(token);
    }

private:
    Token token;
    logger_type lg;
};



template <class Token>
logger_binder<Token> bind_logger(const logger& lg, Token&& token) {
    // Convenience function to bind a logger to a token/handler.
    return {std::forward<Token>(token), lg};
}



template <class Token, class Signature>
class logger_handler;

template <class Token, class... Params>
class logger_handler<Token, void(Params...)> {
public:
    using handler_type = typename boost::asio::handler_type<Token, void(Params...)>::type;

    logger_handler(logger_binder<Token> token)
        : h(token.release_token())
        , lg(token.get_logger())
    {}

    void operator()(Params&&... ps) {
        h(std::forward<Params>(ps)...);
    }

    handler_type& handler() { return h; }

    using logger_type = logger;
    logger_type get_logger() const { return lg; }

    friend void* asio_handler_allocate(size_t size, logger_handler* self) {
        return beast_asio_helpers::allocate(size, self->handler());
    }

    friend void asio_handler_deallocate(void* pointer, size_t size, logger_handler* self) {
        beast_asio_helpers::deallocate(pointer, size, self->handler());
    }

    template <class Function>
    friend void asio_handler_invoke(Function&& f, logger_handler* self) {
        beast_asio_helpers::invoke(std::forward<Function>(f), self->handler());
    }

    friend bool asio_handler_is_continuation(logger_handler* self) {
        return beast_asio_helpers::is_continuation(self->handler());
    }

private:
    handler_type h;
    logger lg;
};

}  // composed



namespace boost { namespace asio {

template <class Token, class Signature>
struct async_result<::composed::logger_handler<Token, Signature>> {
    using handler_type = typename ::composed::logger_handler<Token, Signature>::handler_type;

public:
    using type = typename async_result<handler_type>::type;
    async_result(::composed::logger_handler<Token, Signature>& handler)
        : result(handler.handler())
    {}

    type get() { return result.get(); }

private:
    async_result<handler_type> result;
};

template <class Token, class Signature>
struct handler_type<::composed::logger_binder<Token>, Signature> {
    using type = ::composed::logger_handler<Token, Signature>;
};

}}  // boost::asio
#endif
#endif
