#ifndef UTIL_ASYNCCOMPLETION_HPP
#define UTIL_ASYNCCOMPLETION_HPP

#include <boost/asio/async_result.hpp>

#include <type_traits>

namespace util {

// This is basically just async_result_init/async_completion from Asio.
template <class CompletionToken, class Signature>
struct AsyncCompletion {
    /// The real handler type to be used for the asynchronous operation.
    using HandlerType = typename boost::asio::handler_type<CompletionToken, Signature>::type;

    template <class If, class Else>
    using IfTokenIsHandler
        = typename std::conditional<
            std::is_same<CompletionToken, HandlerType>::value, If, Else
        >::type;

    explicit AsyncCompletion (typename std::remove_reference<CompletionToken>::type& origHandler)
        : handler(static_cast<IfTokenIsHandler<HandlerType&, CompletionToken&&>>(origHandler))
        , result(handler) {}

    /// A copy of, or reference to, a real handler object.
    IfTokenIsHandler<HandlerType&, HandlerType> handler;

    /// The result of the asynchronous operation's initiating function.
    boost::asio::async_result<HandlerType> result;
};

} // namespace util

#endif
