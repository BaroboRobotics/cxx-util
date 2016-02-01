#ifndef UTIL_READFILE_HPP
#define UTIL_READFILE_HPP

#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>

#include <sstream>
#include <string>

namespace util {

// Read an entire file, identified by path, into a string.
static inline std::string readFile (const boost::filesystem::path& path) {
    boost::filesystem::ifstream ifs{path};
    auto ss = std::stringstream{};
    ss << ifs.rdbuf();
    return ss.str();
}

} // namespace util

#endif