#ifndef UTIL_READFILE_HPP
#define UTIL_READFILE_HPP

#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>

#include <sstream>
#include <string>

namespace util {

// Read an entire file, identified by path, into a string.
// Straight stolen from https://github.com/Oberon00/synth/blob/master/src/main.cpp
static std::string readFile (const boost::filesystem::path& path) {
    boost::filesystem::ifstream ifs{path, std::ios::in | std::ios::binary};
    ifs.exceptions(std::ios::badbit);
    std::ostringstream contents;
    contents << ifs.rdbuf();
    return std::move(contents).str();
}

} // namespace util

#endif
