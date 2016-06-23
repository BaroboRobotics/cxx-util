#ifndef UTIL_PROGRAMPATH_HPP
#define UTIL_PROGRAMPATH_HPP

#include <boost/filesystem.hpp>

namespace util {

boost::filesystem::path programPath ();
// Return a canonical path to the program file containing the main() function of the current
// process.

} // util

#endif
