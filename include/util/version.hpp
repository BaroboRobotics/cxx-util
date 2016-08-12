// Copyright (c) 2016 Barobo, Inc.
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#ifndef UTIL_VERSION_HPP
#define UTIL_VERSION_HPP

#include <boost/optional.hpp>
#include <boost/variant.hpp>

#include <iostream>
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
    using PreReleaseContainer = std::vector<boost::variant<unsigned, std::string>>;
    using IdentifierContainer = std::vector<std::string>;

    const auto& numbers () const {
        return std::get<0>(mData);
    }

    const auto& preRelease () const {
        return std::get<1>(mData);
    }

    const auto& buildMetadata () const {
        return std::get<2>(mData);
    }

private:
    std::tuple<NumberContainer,
        boost::optional<PreReleaseContainer>,
        boost::optional<IdentifierContainer>> mData;
};

// `Version`s are EqualityComparable and LessThanComparable.

bool operator== (const Version& a, const Version& b);
bool operator< (const Version& a, const Version& b);

// And we'll provide the rest of the operators to be nice.

inline bool operator!= (const Version& a, const Version& b) {
    return !(a == b);
}

inline bool operator> (const Version& a, const Version& b) {
    return b < a;
}

inline bool operator<= (const Version& a, const Version& b) {
    return a < b || a == b;
}

inline bool operator>= (const Version& a, const Version& b) {
    return a > b || a == b;
}

std::ostream& operator<< (std::ostream&, const Version&);

} // util

#endif
