#ifndef UTIL_TOHEXSTRING_HPP
#define UTIL_TOHEXSTRING_HPP

#include <iomanip>
#include <sstream>
#include <string>

namespace util {

template <class Iter>
std::string toHexString (Iter b, Iter e) {
    auto ss = std::ostringstream();
    ss << std::hex << std::setw(2);
    if (b != e) { ss << static_cast<int>(*b++); }
    for (; b != e; ++b) {
        ss << " " << static_cast<int>(*b);
    }
    return ss.str();
}

} // util

#endif