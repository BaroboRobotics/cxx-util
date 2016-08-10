// Copyright (c) 2014-2016 Barobo, Inc.
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#ifndef UTIL_VERSION_HPP
#define UTIL_VERSION_HPP

#include <boost/system/error_code.hpp>
#include <boost/system/system_error.hpp>

#include <boost/array.hpp>

#include <string>

namespace util {

class Version {
public:
    Version () = default;

    explicit Version (const std::string& triplet);

    friend bool operator== (const Version& lhs, const Version& rhs);
    friend bool operator!= (const Version& lhs, const Version& rhs);
    friend bool operator< (const Version& lhs, const Version& rhs);
    friend bool operator<= (const Version& lhs, const Version& rhs);
    friend bool operator> (const Version& lhs, const Version& rhs);
    friend bool operator>= (const Version& lhs, const Version& rhs);

    enum class Status {
        OK,
        LEADING_ZERO,
        NEGATIVE_INTEGER
    };

private:
    boost::array<unsigned, 3> mTriplet {};
};

class VersionErrorCategory : public boost::system::error_category {
public:
    virtual const char* name () const BOOST_NOEXCEPT override;
    virtual std::string message (int ev) const BOOST_NOEXCEPT override;
};

const boost::system::error_category& versionErrorCategory ();
boost::system::error_code make_error_code (Version::Status status);
boost::system::error_condition make_error_condition (Version::Status status);

} // util

namespace boost { namespace system {
    template <>
    struct is_error_code_enum< ::util::Version::Status> : public std::true_type { };
}} // boost::system

#endif
