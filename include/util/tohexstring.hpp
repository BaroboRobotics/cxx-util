// Copyright (c) 2014-2016 Barobo, Inc.
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

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
