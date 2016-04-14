#ifndef UTIL_ASIO_TRANSPARENTSERVICE_HPP
#define UTIL_ASIO_TRANSPARENTSERVICE_HPP

#include <util/asio/workcompletiontoken.hpp>

#include <boost/asio/io_service.hpp>

#include <memory>
#include <utility>

namespace util {

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
        close(impl, ec);
        impl.reset();
    }

    void close (implementation_type& impl, boost::system::error_code& ec) {
        impl->close(ec);
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
};

} // namespace util

#endif
