// Copyright (c) 2014-2016 Barobo, Inc.
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#ifndef UTIL_PROGRAMPATH_HPP
#define UTIL_PROGRAMPATH_HPP

#include <boost/filesystem.hpp>

namespace util {

boost::filesystem::path programPath ();
// Return a canonical path to the program file containing the main() function of the current
// process.

} // util

#endif
