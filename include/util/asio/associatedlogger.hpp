// Copyright (c) 2014-2016 Barobo, Inc.
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#ifndef UTIL_ASIO_ASSOCIATEDLOGGER_HPP
#define UTIL_ASIO_ASSOCIATEDLOGGER_HPP

#include <util/asio/handler_hooks.hpp>
#include <util/log.hpp>

#include <boost/asio/io_service.hpp>
#include <boost/asio/async_result.hpp>

#include <utility>
#include <functional>

namespace util { namespace asio {

inline namespace v2 {

namespace _ {

namespace {
    log::Logger& defaultAssociatedLogger () {
        thread_local log::Logger lg;
        return lg;
    }
}

template <class T>
log::Logger& getAssociatedLogger (const T& t) {
    return defaultAssociatedLogger();
}

struct GetAssociatedLogger {
    template <class T>
    log::Logger& operator() (const T& t) const {
        return getAssociatedLogger(t);
    }
};

template <class T>
struct StaticConst {
    // Template to ensure an object is weakly linked.
    static constexpr T value {};
};

template <class T>
constexpr T StaticConst<T>::value;

} // _

namespace {
    constexpr auto& getAssociatedLogger = _::StaticConst<_::GetAssociatedLogger>::value;
    // Customization point to associate a logger with an arbitrary second object.
    // The primary use case of this is to associate a logger with an Asio handler, so asynchronous
    // operations can easily create log records with metada only known to the initiating scope.
    // For example, an asynchronous ribbon-bridge operation has no notion of an IP address, so
    // its log messages lack that potentially useful information. If the operation's handler had an
    // associated logger with an "IpAddress" attribute set, the operation could create log records
    // tagged with the IP address without having to know the information itself.
    //
    // Adapted from a customization point implementation by Eric Niebler:
    //   http://ericniebler.com/2014/10/21/customization-point-design-in-c11-and-beyond/
}

namespace _ {

template <class CompletionToken>
class LoggerCompletionToken {
    // A LoggerCompletionToken attaches a `util::log::Logger` object to a wrapped completion token.
    // When the LoggerCompletionToken is converted to a handler object (e.g., through
    // `util::asio::AsyncCompletion`), the logger is copied over to the handler object. Code with
    // access to the handler can access this logger with `util::asio::getAssociatedLogger(handler)`.

public:
    template <class CT>
    LoggerCompletionToken (CT&& token, log::Logger lg)
        : mToken(std::forward<CT>(token))
        , mLog(std::move(lg))
    {}

    auto& log () const { return mLog; }
    CompletionToken original () const { return mToken; }

private:
    CompletionToken mToken;
    mutable log::Logger mLog;
};

template <class CompletionToken, class Signature>
class LoggerHandler {
public:
    using WrappedHandlerType
        = typename boost::asio::handler_type<CompletionToken, Signature>::type;

    LoggerHandler (LoggerCompletionToken<CompletionToken> token)
        : mHandler(token.original())
        , mLog(token.log())
    {}

    template <class... Params>
    void operator() (Params&&... ps) {
        mHandler(std::forward<Params>(ps)...);
    }

    log::Logger& log () const { return mLog; }
    WrappedHandlerType& original () { return mHandler; }

    friend void* asio_handler_allocate (size_t size, LoggerHandler* self) {
        return handler_hooks::allocate(size, self->original());
    }

    friend void asio_handler_deallocate (void* pointer, size_t size, LoggerHandler* self) {
        handler_hooks::deallocate(pointer, size, self->original());
    }

    template <class Function>
    friend void asio_handler_invoke (Function&& f, LoggerHandler* self) {
        handler_hooks::invoke(std::forward<Function>(f), self->original());
    }

    friend bool asio_handler_is_continuation (LoggerHandler* self) {
        return handler_hooks::is_continuation(self->original());
    }

    friend log::Logger& getAssociatedLogger (const LoggerHandler& self) {
        return self.log();
    }

private:
    WrappedHandlerType mHandler;
    mutable log::Logger mLog;
};

} // _

template <class CompletionToken>
_::LoggerCompletionToken<CompletionToken>
addAssociatedLogger (CompletionToken&& token, const log::Logger& lg = {}) {
    return { std::forward<CompletionToken>(token), lg };
}

}}} // namespace util::asio::v2

namespace boost { namespace asio {

template <class CompletionToken, class Signature>
struct async_result<::util::asio::_::LoggerHandler<CompletionToken, Signature>> {
    using WrappedHandlerType
        = typename ::util::asio::_::LoggerHandler<CompletionToken, Signature>::WrappedHandlerType;

public:
    using type = typename async_result<WrappedHandlerType>::type;
    async_result (::util::asio::_::LoggerHandler<CompletionToken, Signature>& handler)
        : mResult(handler.original())
    {}

    type get () { return mResult.get(); }

private:
    async_result<WrappedHandlerType> mResult;
};

template <class CompletionToken, class Signature>
struct handler_type<::util::asio::_::LoggerCompletionToken<CompletionToken>, Signature> {
    using type = ::util::asio::_::LoggerHandler<CompletionToken, Signature>;
};

}} // namespace boost::asio

#endif
