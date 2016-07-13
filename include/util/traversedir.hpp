#ifndef UTIL_TRAVERSEDIR_HPP
#define UTIL_TRAVERSEDIR_HPP

#include <boost/filesystem.hpp>
#include <boost/range/iterator_range.hpp>

namespace util {

inline boost::iterator_range<boost::filesystem::directory_iterator>
traverseDir (const boost::filesystem::path& root) {
    // Return a range containing entries in the directory `root`.
    return boost::make_iterator_range(
        boost::filesystem::directory_iterator{root},
        boost::filesystem::directory_iterator{});
};

inline boost::iterator_range<boost::filesystem::recursive_directory_iterator>
traverseDirR (const boost::filesystem::path& root) {
    // Return a range containing entries in the directory `root`, and all its subdirectories.
    return boost::make_iterator_range(
        boost::filesystem::recursive_directory_iterator{root},
        boost::filesystem::recursive_directory_iterator{});
};

} // util

#endif
