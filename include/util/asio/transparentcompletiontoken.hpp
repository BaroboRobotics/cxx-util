#ifndef UTIL_ASIO_WORKCOMPLETIONTOKEN_HPP
#define UTIL_ASIO_WORKCOMPLETIONTOKEN_HPP

#include <util/asio/handler_hooks.hpp>

#include <boost/asio/io_service.hpp>
#include <boost/asio/async_result.hpp>

#include <utility>
#include <functional>

namespace util { namespace asio {

// Wrap another asynchronous completion token, along with a reference to an io_service. When this
// completion token is converted to a handler object (e.g., through util::AsyncCompletion), a work
// object for the wrapped io_service is attached to the handler object. This is useful to keep an
// io_service::run() thread from exiting while an asynchronous operation is underway.
//
// The primary use case for WorkCompletionToken is in the implementation of a generic service --
// it is unlikely a user will find this useful directly.
template <class CompletionToken>
class WorkCompletionToken {
public:
    template <class CT>
    WorkCompletionToken (boost::asio::io_service& context, CT&& token)
        : mContext(context)
        , mToken(std::forward<CT>(token))
    {}

    boost::asio::io_service& context () const { return mContext; }
    CompletionToken original () const { return mToken; }

private:
    boost::asio::io_service& mContext;
    CompletionToken mToken;
};

template <class CompletionToken, class Signature>
class WorkHandler {
public:
    using WrappedHandlerType
        = typename boost::asio::handler_type<CompletionToken, Signature>::type;

    WorkHandler (WorkCompletionToken<CompletionToken> token)
        : mWork(token.context())
        , mHandler(token.original())
    {}

    // Implicitly generated copy/move ctors/operations are fine. If the compiler tells you the copy
    // constructor is deleted, it's likely because mHandler is noncopyable. Check for raw
    // references in std::bind expressions (they must be wrapped in std::ref or std::cref).

    template <class... Params>
    void operator() (Params&&... ps) {
        mWork.get_io_service().dispatch(std::bind(mHandler, std::forward<Params>(ps)...));
    }

    WrappedHandlerType& original () { return mHandler; }

    friend void* asio_handler_allocate (size_t size, WorkHandler* self) {
        return handler_hooks::allocate(size, self->original());
    }

    friend void asio_handler_deallocate (void* pointer, size_t size, WorkHandler* self) {
        handler_hooks::deallocate(pointer, size, self->original());
    }

    template <class Function>
    friend void asio_handler_invoke (Function&& f, WorkHandler* self) {
        handler_hooks::invoke(std::forward<Function>(f), self->original());
    }

    friend bool asio_handler_is_continuation (WorkHandler* self) {
        return handler_hooks::is_continuation(self->original());
    }

private:
    boost::asio::io_service::work mWork;
    WrappedHandlerType mHandler;
};

}} // namespace util::asio

namespace boost { namespace asio {

template <class CompletionToken, class Signature>
struct async_result<::util::asio::WorkHandler<CompletionToken, Signature>> {
    using WrappedHandlerType
        = typename ::util::asio::WorkHandler<CompletionToken, Signature>::WrappedHandlerType;

public:
    using type = typename async_result<WrappedHandlerType>::type;
    async_result (::util::asio::WorkHandler<CompletionToken, Signature>& handler)
        : mResult(handler.original())
    {}

    type get () { return mResult.get(); }

private:
    async_result<WrappedHandlerType> mResult;
};

template <class CompletionToken, class Signature>
struct handler_type<::util::asio::WorkCompletionToken<CompletionToken>, Signature> {
    using type = ::util::asio::WorkHandler<CompletionToken, Signature>;
};

}} // namespace boost::asio

#endif
