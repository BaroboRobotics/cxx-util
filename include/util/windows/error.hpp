// Copyright (c) 2014-2016 Barobo, Inc.
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#ifndef UTIL_WINDOWS_ERROR_HPP
#define UTIL_WINDOWS_ERROR_HPP

#ifndef _WIN32
#error This is a Windows-specific header file.
#endif

#include <windows.h>
//#include <tchar.h>

#include <exception>
#include <string>
#include <memory>

namespace util { namespace windows {

static inline std::string errorString (DWORD code) {
    char* errorText = nullptr;
    auto nWritten = FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM
            | FORMAT_MESSAGE_ALLOCATE_BUFFER
            | FORMAT_MESSAGE_IGNORE_INSERTS,
            nullptr, code,
            MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
            LPSTR(&errorText),
            0, nullptr);
    if (errorText && nWritten) {
        std::shared_ptr<void> guard { nullptr, [&] (void*) {LocalFree(errorText);}};
        return std::string(errorText, errorText + nWritten);
    }
    else {
        return std::string("Windows error ") + std::to_string(GetLastError())
            + " while formatting message for error " + std::to_string(code);
    }
}

struct Error : std::runtime_error {
    Error (std::string prefix, DWORD code)
        : std::runtime_error{prefix + ": " + errorString(code)}
    {}
};

}} // util::windows

#endif
