// Copyright (c) 2014-2016 Barobo, Inc.
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#ifndef UTIL_VERSION_HPP
#define UTIL_VERSION_HPP

#include <boost/optional.hpp>

#include <string>
#include <tuple>
#include <vector>

namespace util {

class Version {
public:
    Version () = default;
    explicit Version (const std::string&);

    bool parse (const std::string&);

    using NumberContainer = std::vector<unsigned>;
    using IdentifierContainer = std::vector<std::string>;

    const NumberContainer& numbers () const {
        return std::get<0>(mData);
    }

    const boost::optional<IdentifierContainer>& preRelease () const {
        return std::get<1>(mData);
    }

    const boost::optional<IdentifierContainer>& buildMetadata () const {
        return std::get<2>(mData);
    }

private:
    std::tuple<NumberContainer,
        boost::optional<IdentifierContainer>,
        boost::optional<IdentifierContainer>> mData;
};

inline bool operator== (const Version& a, const Version& b) {
    return a.numbers() == b.numbers() && a.preRelease() == b.preRelease();
}

inline bool operator!= (const Version& a, const Version& b) {
    return !(a == b);
}

inline bool operator< (const Version& a, const Version& b) {
    if (a.numbers() == b.numbers()) {
        if (a.preRelease() && b.preRelease()) {
            return a.preRelease() < b.preRelease();
        }
        else if (!a.preRelease() && !b.preRelease()) {
            return false;  // equal
        }
        else {
            return !!a.preRelease();  // if `a` has pre-release data, it is less than `b`
        }
    }
    else {
        return a.numbers() < b.numbers();
    }
}

inline bool operator<= (const Version& a, const Version& b) {
    return a < b || a == b;
}

inline bool operator> (const Version& a, const Version& b) {
    if (a.numbers() == b.numbers()) {
        if (a.preRelease() && b.preRelease()) {
            return a.preRelease() > b.preRelease();
        }
        else if (!a.preRelease() && !b.preRelease()) {
            return false;  // equal
        }
        else {
            return !!b.preRelease();  // if `b` has pre-release data, it is less than `a`
        }
    }
    else {
        return a.numbers() > b.numbers();
    }
}

inline bool operator>= (const Version& a, const Version& b) {
    return a > b || a == b;
}

} // util

#endif
