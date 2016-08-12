// Copyright (c) 2016 Barobo, Inc.
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#include <util/version.hpp>

#include <iostream>

#include <cassert>

using util::Version;

#ifdef NDEBUG
static_assert(!NDEBUG, "");
#endif

int main () try {
    // Comments taken from the Semantic Versioning Specification at semver.org.

    // 2. A normal version number MUST take the form X.Y.Z where X, Y, and Z are non-negative
    // integers, and MUST NOT contain leading zeroes. X is the major version, Y is the minor
    // version, and Z is the patch version. Each element MUST increase numerically. For instance:
    // 1.9.0 -> 1.10.0 -> 1.11.0.
    {
        auto v = Version{"1.9.0"};
        auto w = Version{"1.10.0-dev"};
        auto x = Version{"1.10.0"};

        assert(v == Version{v});
        assert(w == Version{w});
        assert(Version{v} == v);
        assert(Version{w} == w);

        assert(v != w);
        assert(v != x);
        assert(w != v);
        assert(w != x);
        assert(x != v);
        assert(x != w);

        assert(v < w);
        assert(w > v);

        assert(v < x);
        assert(x > v);

        assert(w < x);
        assert(x > w);
    }

    return 0;
}
catch (std::exception& e) {
    std::cout << "Unhandled exception: " << e.what() << '\n';
}
