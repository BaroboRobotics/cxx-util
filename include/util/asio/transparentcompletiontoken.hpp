#ifndef UTIL_ASIO_TRANSPARENTCOMPLETIONTOKEN_HPP
#define UTIL_ASIO_TRANSPARENTCOMPLETIONTOKEN_HPP

#include <util/asio/handler_hooks.hpp>

#include <boost/asio/io_service.hpp>
#include <boost/asio/async_result.hpp>

#include <utility>
#include <functional>

namespace util { namespace asio {

// A TransparentCompletionToken attaches an io_service context and an implementation shared_ptr to
// a wrapped completion token. When the TransparentCompletionToken is converted to a handler object
// (e.g., through util::asio::AsyncCompletion), a work object for the io_service and a copy of the
// implementation shared_ptr are stored inside the handler. This guarantees two useful conditions:
//
//   1. io_service::run() on the given io_service will not return until after the handler has been
//      invoked and destroyed.
//   2. The implementation object will not be destroyed until after the handler has been invoked
//      and destroyed.
//
// The primary use case for TransparentCompletionToken is in the implementation of a generic
// service -- it is unlikely a user will find this useful directly.
template <class CompletionToken>
class TransparentCompletionToken {
public:
    template <class CT>
    TransparentCompletionToken (
            boost::asio::io_service& context, std::shared_ptr<void> impl, CT&& token)
        : mContext(context)
        , mToken(std::forward<CT>(token))
    {}

    boost::asio::io_service& context () const { return mContext; }
    std::shared_ptr<void> impl () const { return mImpl; }
    CompletionToken original () const { return mToken; }

private:
    boost::asio::io_service& mContext;
    std::shared_ptr<void> mImpl;
    CompletionToken mToken;
};

template <class CompletionToken, class Signature>
class TransparentHandler {
public:
    using WrappedHandlerType
        = typename boost::asio::handler_type<CompletionToken, Signature>::type;

    TransparentHandler (TransparentCompletionToken<CompletionToken> token)
        : mWork(token.context())
        , mImpl(token.impl())
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

    friend void* asio_handler_allocate (size_t size, TransparentHandler* self) {
        return handler_hooks::allocate(size, self->original());
    }

    friend void asio_handler_deallocate (void* pointer, size_t size, TransparentHandler* self) {
        handler_hooks::deallocate(pointer, size, self->original());
    }

    template <class Function>
    friend void asio_handler_invoke (Function&& f, TransparentHandler* self) {
        handler_hooks::invoke(std::forward<Function>(f), self->original());
    }

    friend bool asio_handler_is_continuation (TransparentHandler* self) {
        return handler_hooks::is_continuation(self->original());
    }

private:
    boost::asio::io_service::work mWork;
    std::shared_ptr<void> mImpl;
    // No need to actually know mImpl's implementation type -- we just want it destroyed sometime
    // after the handler. This allows TransparentIoObject implementations to capture this in a
    // lambda without having to worry about saving this->shared_from_this().
    WrappedHandlerType mHandler;
};

}} // namespace util::asio

namespace boost { namespace asio {

template <class CompletionToken, class Signature>
struct async_result<::util::asio::TransparentHandler<CompletionToken, Signature>> {
    using WrappedHandlerType
        = typename ::util::asio::TransparentHandler<CompletionToken, Signature>::WrappedHandlerType;

public:
    using type = typename async_result<WrappedHandlerType>::type;
    async_result (::util::asio::TransparentHandler<CompletionToken, Signature>& handler)
        : mResult(handler.original())
    {}

    type get () { return mResult.get(); }

private:
    async_result<WrappedHandlerType> mResult;
};

template <class CompletionToken, class Signature>
struct handler_type<::util::asio::TransparentCompletionToken<CompletionToken>, Signature> {
    using type = ::util::asio::TransparentHandler<CompletionToken, Signature>;
};

}} // namespace boost::asio

#endif
