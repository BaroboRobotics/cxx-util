#ifndef UTIL_ASIO_TRANSPARENTSERVICE_HPP
#define UTIL_ASIO_TRANSPARENTSERVICE_HPP

#include <util/asio/workcompletiontoken.hpp>
#include <util/index_sequence.hpp>

#include <boost/asio/io_service.hpp>

#include <memory>
#include <utility>

namespace util { namespace asio {

// A class which meets the minimum requirements of an Asio I/O object service. It uses the same
// io_service event loop passed in to it as the event loop for the I/O object implementation, thus
// internal handlers get posted to the user's event loop!
template <class Impl>
class TransparentService : public boost::asio::io_service::service {
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
        impl->close(ec);
        impl.reset();
    }

    template <class CompletionToken>
    WorkCompletionToken<typename std::decay<CompletionToken>::type>
    transformCompletionToken (CompletionToken&& token) {
        return WorkCompletionToken<typename std::decay<CompletionToken>::type>{
            get_io_service(), std::forward<CompletionToken>(token)
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

    void close () {
        boost::system::error_code ec;
        close(ec);
        if (ec) {
            throw boost::system::system_error(ec);
        }
    }

    void close (boost::system::error_code& ec) {
        this->get_implementation()->close(ec);
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
        return this->get_implementation()->methodName( \
            std::get<NMinusOneIndices>(t)..., \
            this->get_service().transformCompletionToken( \
                std::get<std::tuple_size<typename std::decay<Tuple>::type>::value - 1>(t))); \
    }

#endif
