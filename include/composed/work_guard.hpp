// Copyright (c) 2017 Barobo, Inc.
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#ifndef COMPOSED_WORK_GUARD_HPP
#define COMPOSED_WORK_GUARD_HPP

#include <composed/stdlib.hpp>

#include <boost/assert.hpp>

#include <utility>

namespace composed {

template <class Executor>
class work_guard {
    // Value type that owns work in an executor.

public:
    work_guard() = default;

    explicit work_guard(Executor& e): exec(&e) { exec->on_work_started(); }

    work_guard(const work_guard& other): exec(other.exec) { if (exec) { exec->on_work_started(); } }

    work_guard(work_guard&& other) {
        using std::swap;
        swap(*this, other);
    }

    work_guard& operator=(work_guard other) {
        using std::swap;
        swap(*this, other);
        return *this;
    }

    ~work_guard() { if (exec) { exec->on_work_finished(); } }

    friend void swap(work_guard& a, work_guard& b) noexcept {
        using std::swap;
        swap(a.exec, b.exec);
    }

private:
    Executor* exec = nullptr;
};

template <class Executor>
work_guard<Executor> make_work_guard(Executor& e) {
    return work_guard<Executor>{e};
}

}  // composed

#endif