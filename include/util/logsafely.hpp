#ifndef UTIL_LOGSAFELY_HPP
#define UTIL_LOGSAFELY_HPP

#include <boost/log/attributes/clock.hpp>
#include <boost/log/attributes/timer.hpp>
#include <boost/log/utility/formatting_ostream.hpp>
#include <boost/log/utility/manipulators/to_log.hpp>

#include <iomanip>

namespace util {

// Boost.Log uses Boost.DateTime's ptime class for clock and timer values.
// DateTime uses setlocale(), which is not thread-safe under Windows XP. To
// avoid setlocale() related crashes, we provide a pair of safe operator<<
// overloads for formatting Boost.Log clocks and timers according to ISO 8601.

// To select these overloads, our formatter would use the form:
//   expr::attr<attrs::timer::value_type, LogSafely>("Timeline")
// or
//   expr::attr<attrs::clock::value_type, LogSafely>("TimeStamp")

struct LogSafely;
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

using LocalClockValueType = boost::log::attributes::local_clock::value_type;
using SafeLocalClockToLogManip = boost::log::to_log_manip<LocalClockValueType, LogSafely>;

FormatStream& operator<< (FormatStream& os, const SafeLocalClockToLogManip& manip) {
	auto& t = manip.get();
	os << std::setfill('0')
	   << std::setw(4) << t.date().year() << '-'
	   << std::setw(2) << int(t.date().month()) << '-'
	   << std::setw(2) << t.date().day() << 'T'
	   << SafeTimerToLogManip{t.time_of_day()};
   return os;
}

} // namespace util

#endif