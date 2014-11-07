#ifndef UTIL_TOPOSIXTIME_HPP
#define UTIL_TOPOSIXTIME_HPP

#include <boost/date_time/posix_time/posix_time_types.hpp>
#include <chrono>

namespace util {

template <typename Rep, typename Period>
boost::posix_time::time_duration toPosixTime (const std::chrono::duration<Rep, Period>& from) {
    auto d = std::chrono::duration_cast<std::chrono::nanoseconds>(from).count();
    auto sec = d / 1000000000;
    auto nsec = d % 1000000000;
    return boost::posix_time::seconds(static_cast<long long>(sec)) +
#ifdef BOOST_DATE_TIME_HAS_NANOSECONDS
        boost::posix_time::nanoseconds(nsec);
#else
        boost::posix_time::microseconds((nsec+(nsec>0?500:-500))/1000);
#endif
}

} // namespace util

#endif
