// Copyright (c) 2014-2016 Barobo, Inc.
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#include <util/version.hpp>

#include <boost/system/error_code.hpp>

#include <iostream>

#include <cassert>

using util::Version;

template <class... Args>
void expect (boost::system::error_code expected, Args&&... args) {
    try {
        auto v = Version{std::forward<Args>(args)...};
    }
    catch (boost::system::system_error& e) {
        if (e.code() != expected) {
            throw;
        }
    }
}

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
        auto w = Version{"1.10.0"};

        assert(v < w);

#if 0
        v = v.bumpMajor();
        v = v.bumpMinor();
        v = v.bumpPatch();
        ++v.major();
        ++v.minor();
        ++v.patch();

        assert(v.major() == x + 1);
        assert(v.minor() == y + 1);
        assert(v.patch() == z + 1);
#endif
    }

#if 0
    expect(util::Version::Status::NEGATIVE_INTEGER, "-1.9.0");
    expect(util::Version::Status::NEGATIVE_INTEGER, "1.-9.0");
    expect(util::Version::Status::NEGATIVE_INTEGER, "1.9.-0");

    expect(util::Version::Status::NEGATIVE_INTEGER, "-1.-9.0");
    expect(util::Version::Status::NEGATIVE_INTEGER, "-1.9.-0");
    expect(util::Version::Status::NEGATIVE_INTEGER, "1.-9.-0");

    expect(util::Version::Status::NEGATIVE_INTEGER, "-1.-9.-0");

    expect(util::Version::Status::LEADING_ZERO, "01.9.0");
    expect(util::Version::Status::LEADING_ZERO, "1.09.0");
    expect(util::Version::Status::LEADING_ZERO, "1.9.00");

    expect(util::Version::Status::LEADING_ZERO, "01.09.0");
    expect(util::Version::Status::LEADING_ZERO, "01.9.00");
    expect(util::Version::Status::LEADING_ZERO, "1.09.00");

    expect(util::Version::Status::LEADING_ZERO, "01.09.00");
#endif

    return 0;
}
catch (std::exception& e) {
    std::cout << "Unhandled exception: " << e.what() << '\n';
}
