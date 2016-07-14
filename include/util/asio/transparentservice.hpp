#ifndef UTIL_ASIO_TRANSPARENTSERVICE_HPP
#define UTIL_ASIO_TRANSPARENTSERVICE_HPP

#include <util/asio/handler_hooks.hpp>
#include <util/asio/associatedlogger.hpp>
#include <util/index_sequence.hpp>

#include <boost/asio/async_result.hpp>
#include <boost/asio/io_service.hpp>
#include <boost/asio/basic_io_object.hpp>

#include <functional>
#include <memory>
#include <utility>

namespace util { namespace asio {

inline namespace v2 {

namespace _ {

template <class CompletionToken>
class TransparentCompletionToken {
    // A `TransparentCompletionToken` attaches an `io_service` context and an implementation
    // `shared_ptr` to a wrapped completion token. When the `TransparentCompletionToken` is
    // converted to a handler object (e.g., through `util::asio::AsyncCompletion`), a work object
    // for the `io_service` and a copy of the implementation `shared_ptr` are stored inside the
    // handler. This guarantees two useful conditions:
    //
    //   1. `io_service::run()`` on the given `io_service` will not return until after the handler
    //      has been invoked and destroyed.
    //   2. The implementation object will not be destroyed until after the handler has been
    //      invoked and destroyed.
    //
    // The primary use case for `TransparentCompletionToken` is in the implementation of a generic
    // service -- it is unlikely a user will find this useful directly.

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

    friend log::Logger& getAssociatedLogger (const TransparentHandler& self) {
        return getAssociatedLogger(self.mHandler);
    }

private:
    boost::asio::io_service::work mWork;
    std::shared_ptr<void> mImpl;
    // No need to actually know mImpl's implementation type -- we just want it destroyed sometime
    // after the handler. This allows TransparentIoObject implementations to capture this in a
    // lambda without having to worry about saving this->shared_from_this().
    WrappedHandlerType mHandler;
};

}}}} // util::asio::v2::_

namespace boost { namespace asio {

template <class CompletionToken, class Signature>
struct async_result<::util::asio::_::TransparentHandler<CompletionToken, Signature>> {
    using WrappedHandlerType = typename ::util::asio::_::TransparentHandler<
            CompletionToken, Signature>::WrappedHandlerType;

public:
    using type = typename async_result<WrappedHandlerType>::type;
    async_result (::util::asio::_::TransparentHandler<CompletionToken, Signature>& handler)
        : mResult(handler.original())
    {}

    type get () { return mResult.get(); }

private:
    async_result<WrappedHandlerType> mResult;
};

template <class CompletionToken, class Signature>
struct handler_type<::util::asio::_::TransparentCompletionToken<CompletionToken>, Signature> {
    using type = ::util::asio::_::TransparentHandler<CompletionToken, Signature>;
};

}} // boost::asio

namespace util { namespace asio {

template <class Impl>
class TransparentService : public boost::asio::io_service::service {
    // A class which meets the minimum requirements of an Asio I/O object service. It uses the same
    // io_service event loop passed in to it as the event loop for the I/O object implementation,
    // thus internal handlers get posted to the user's event loop!

public:
    using implementation_type = std::shared_ptr<Impl>;
    static boost::asio::io_service::id id;

    explicit TransparentService (boost::asio::io_service& ios)
        : boost::asio::io_service::service(ios)
    {}

    void construct (implementation_type& impl) {
        impl = std::make_shared<Impl>(this->get_io_service());
    }

    void move_construct (implementation_type& impl, implementation_type& other) {
        impl = std::move(other);
    }

    void destroy (implementation_type& impl) {
        auto ec = boost::system::error_code{};
        close(impl, ec);
        impl.reset();
    }

    void close (implementation_type& impl, boost::system::error_code& ec) {
        ec = boost::system::error_code{};
        if (impl) {
            impl->close(ec);
        }
    }

    template <class CompletionToken>
    auto transformCompletionToken (implementation_type impl, CompletionToken&& token) {
        // Make sure the event loop's work flag is set, and the implementation object is alive,
        // for the duration of the operation.
        return _::TransparentCompletionToken<std::decay_t<CompletionToken>>{
            get_io_service(),
            std::move(impl),
            std::forward<CompletionToken>(token)
        };
    }

private:
    void shutdown_service () {}
};

template <class Impl>
boost::asio::io_service::id TransparentService<Impl>::id;

template <class Impl>
struct TransparentIoObject : boost::asio::basic_io_object<TransparentService<Impl>> {
    explicit TransparentIoObject (boost::asio::io_service& ios)
        : boost::asio::basic_io_object<TransparentService<Impl>>(ios)
    {}

    TransparentIoObject (const TransparentIoObject&) = delete;
    TransparentIoObject& operator= (const TransparentIoObject&) = delete;

    TransparentIoObject (TransparentIoObject&&) = default;
    TransparentIoObject& operator= (TransparentIoObject&&) = default;
    // Moving from a `TransparentIoObject` causes all asynchronous operation initiating functions
    // on the moved-from object to have undefined behavior, due to a null pointer dereference. It
    // is only safe to call the destructor, `close()`, and whatever functions you implement in your
    // derived class which take into account the nullability of `this->get_implementation()`.

    void close () {
        boost::system::error_code ec;
        close(ec);
        if (ec) {
            throw boost::system::system_error(ec);
        }
    }

    void close (boost::system::error_code& ec) {
        this->get_service().close(this->get_implementation(), ec);
    }
};

}} // namespace util::asio

// Define an asynchronous method in the body of an IO object. All arguments will be forwarded
// except for the last, which is the completion token. The completion token is transformed into a
// potentially service-specific completion token before being forwarded to the implementation of
// the IO object.
#define UTIL_ASIO_DECL_ASYNC_METHOD(methodName) \
public: \
    template <class... Args, class Indices = util::make_index_sequence_t<sizeof...(Args) - 1>> \
    auto methodName (Args&&... args) { \
        static_assert(sizeof...(Args) > 0, "Asynchronous operations need at least one argument"); \
        return methodName##Impl(std::forward_as_tuple(std::forward<Args>(args)...), Indices{}); \
    } \
private: \
    template <class Tuple, size_t... NMinusOneIndices> \
    auto methodName##Impl (Tuple&& t, util::index_sequence<NMinusOneIndices...>&&) { \
        auto impl = this->get_implementation(); \
        return impl->methodName( \
            std::get<NMinusOneIndices>(t)..., \
            this->get_service().transformCompletionToken( \
                impl, \
                std::get<std::tuple_size<std::decay_t<Tuple>>::value - 1>(t))); \
    }

#endif
