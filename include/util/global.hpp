// Copyright (c) 2014-2016 Barobo, Inc.
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#ifndef UTIL_GLOBAL_HPP
#define UTIL_GLOBAL_HPP

#include <memory>
#include <mutex>

namespace util {

template <class T>
std::shared_ptr<T> global () {
    // Access a global object of type T. T must be default-constructible. The object is created if
    // it does not yet exist, and destroyed when the last `shared_ptr<T>` returned from
    // `global<T>()` is destroyed.

    static std::mutex mutex;
    std::lock_guard<std::mutex> lock {mutex};

    static auto wp = std::weak_ptr<T>{};
    auto p = wp.lock();
    if (!p) {
        p = std::make_shared<T>();
        wp = p;
    }
    return p;
}

} // namespace util

#endif
