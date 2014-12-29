#ifndef UTIL_LOGSAFELY_HPP
#define UTIL_LOGSAFELY_HPP

#include <boost/log/attributes/timer.hpp>
#include <boost/log/utility/formatting_ostream_fwd.hpp>
#include <boost/log/utility/manipulators/to_log.hpp>

#include <iomanip>

namespace util {

struct LogSafely;

// The default formatter for boost::log::timer::value_type uses setlocale(),
// which is not thread-safe on Windows XP. Boost.Log provides a way to select
// a different operator<< for log record formatting, which we can use to work
// around the problem. To select this overload, our formatter would use the
// form: expr::attr<attrs::timer::value_type, LogSafely>("Timeline").
using FormatStream = boost::log::formatting_ostream;
using TimerValueType = boost::log::attributes::timer::value_type;
using SafeTimerToLogManip = boost::log::to_log_manip<TimerValueType, LogSafely>;

FormatStream& operator<< (FormatStream& os, const SafeTimerToLogManip& manip) {
    auto& t = manip.get();
    os << std::setfill('0')
       << std::setw(2) << t.hours() << ":"
       << std::setw(2) << t.minutes() << ":"
       << std::setw(2) << t.seconds() << "."
       << std::setw(6) << t.fractional_seconds();
    return os;
}

} // namespace util

#endif