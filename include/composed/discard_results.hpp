// Copyright (c) 2017 Barobo, Inc.
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#ifndef COMPOSED_DISCARD_RESULTS_HPP
#define COMPOSED_DISCARD_RESULTS_HPP

namespace composed {

struct discard_results_t {
    template <class... Args>
    void operator()(Args&&...) const {}
};

constexpr discard_results_t discard_results {};

}  // composed

#endif
