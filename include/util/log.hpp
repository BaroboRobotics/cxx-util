#ifndef UTIL_LOG_HPP
#define UTIL_LOG_HPP

#include <boost/log/sources/severity_channel_logger.hpp>
#include <boost/log/sources/record_ostream.hpp>
#include <boost/log/keywords/channel.hpp>

#include <boost/program_options/options_description.hpp>
#include <boost/program_options/variables_map.hpp>

#include <boost/optional.hpp>

#include <string>

namespace util { namespace log {

boost::program_options::options_description optionsDescription ();
// Build and return a description of what command line options the logging system supports. Use the
// returned value to parse the command line -- logging sinks will be automatically configured when
// boost::program_options::notify() is called.
//
// This function also adds some common attributes to the logging core.

using Logger = boost::log::sources::severity_channel_logger<>;

using boost::log::keywords::channel;

namespace _ {

namespace {
    Logger& defaultAssociatedLogger () {
        thread_local Logger lg;
        return lg;
    }
}

template <class T>
Logger& getAssociatedLogger (const T& t) {
    return defaultAssociatedLogger();
}

struct GetAssociatedLogger {
    template <class T>
    Logger& operator() (const T& t) const {
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

}} // namespace util::log

#endif
