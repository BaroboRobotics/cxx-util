// Copyright (c) 2014-2016 Barobo, Inc.
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#include <util/programpath.hpp>

#include <boost/predef.h>

namespace fs = boost::filesystem;

#if BOOST_OS_LINUX

namespace util {

fs::path programPath () {
    return fs::canonical(fs::read_symlink("/proc/self/exe"));
}

} // util

#elif BOOST_OS_WINDOWS

#include <vector>

#include <windows.h>

namespace util {

fs::path programPath () {
    constexpr auto kMaxBufSize = 1 << 16;

    auto buf = std::vector<char>(1);
    decltype(buf)::size_type n;

    n = GetModuleFileName(nullptr, buf.data(), buf.size());
    // On XP, GetModuleFileName() always sets ERROR_SUCCESS, so it's safest to detect failures
    // using just the return value.
    while (buf.size() == n) {
        const auto newSize = 2 * buf.size();
        if (newSize > kMaxBufSize) {
            throw std::runtime_error{"Exceeded buffer size limit while getting program path"};
        }
        buf.resize(newSize);
        n = GetModuleFileName(nullptr, buf.data(), buf.size());
    }

    return fs::canonical(fs::path(buf.data(), buf.data() + n));
}

}
// TODO: implement using GetModuleFileName()

#elif BOOST_OS_MACOS

#error "programPath() is not yet implemented on Mac"
// TODO: implement using _NSGetExecutablePath()
// could also use libproc (proc_pidpath()), but that appears to:
//   1. be a private API
//   2. require that we use a fixed size buffer for the path

#else

#error "I don't recognize your platform"

#endif
