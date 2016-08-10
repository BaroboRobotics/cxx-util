// Copyright (c) 2014-2016 Barobo, Inc.
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#include <util/version.hpp>
#include <util/log.hpp>

#include <boost/spirit/include/phoenix_operator.hpp>
#include <boost/spirit/include/phoenix_fusion.hpp>
#include <boost/spirit/include/phoenix_stl.hpp>
#include <boost/spirit/include/qi.hpp>

#include <boost/fusion/adapted.hpp>
#include <boost/fusion/adapted/boost_array.hpp>

// http://stackoverflow.com/questions/34435150/boostspirit-parsing-into-struct-with-stdarray
namespace boost { namespace spirit { namespace traits {
    template <typename T, size_t N>
        struct is_container<boost::array<T, N>, void> : mpl::false_ { };
}}} // boost::spirit::traits

namespace util {

namespace {

namespace qi = boost::spirit::qi;

template <class Iter>
struct VersionGrammar : qi::grammar<Iter, boost::array<unsigned, 3>()> {
    qi::rule<Iter, boost::array<unsigned, 3>()> start;
    qi::rule<Iter, unsigned()> number;

    VersionGrammar () : VersionGrammar::base_type(start, "version") {
        using qi::_1;
        using qi::_val;
        using boost::phoenix::at_c;

        start.name("start");
        start %= number > '.' > number > '.' > number > qi::eoi;

        number.name("number");
        number %= qi::uint_;

        using ErrorHandlerArgs = boost::fusion::vector<
            Iter&, const Iter&, const Iter&, const qi::info&>;

        auto logError = [](ErrorHandlerArgs args, auto&, qi::error_handler_result&) {
            std::cerr << "Expected '" << at_c<3>(args) << "' here: '"
                << std::string(at_c<2>(args), at_c<1>(args)) << "'\n" << std::endl;
        };

        qi::on_error<qi::fail>(start, logError);
        qi::on_error<qi::fail>(number, logError);
    }
};

#if 0
bool parseVersion (const std::string& triplet, boost::array<unsigned, 3>& xyz) {
    VersionGrammar<decltype(triplet.begin())> grammar;
    return qi::parse(hex.begin(), hex.end(), grammar, xyz);
}
#endif

boost::array<unsigned, 3> parseVersion (const std::string& triplet) {
    VersionGrammar<decltype(triplet.begin())> grammar;
    boost::array<unsigned, 3> numbers;
    if (!qi::parse(triplet.begin(), triplet.end(), grammar, numbers)) {
        throw std::runtime_error{"Version parsing failed"};
    }
    return numbers;
}

} // <anonymous>

Version::Version (const std::string& triplet)
    : mTriplet(parseVersion(triplet))
{}

// Implementation of equality/ordering is trivial: `boost::array` implements lexicographical
// comparison for us already.

bool operator== (const Version& lhs, const Version& rhs) {
    return lhs.mTriplet == rhs.mTriplet;
}

bool operator!= (const Version& lhs, const Version& rhs) {
    return lhs.mTriplet != rhs.mTriplet;
}

bool operator< (const Version& lhs, const Version& rhs) {
    return lhs.mTriplet < rhs.mTriplet;
}

bool operator<= (const Version& lhs, const Version& rhs) {
    return lhs.mTriplet <= rhs.mTriplet;
}

bool operator> (const Version& lhs, const Version& rhs) {
    return lhs.mTriplet > rhs.mTriplet;
}

bool operator>= (const Version& lhs, const Version& rhs) {
    return lhs.mTriplet >= rhs.mTriplet;
}

const char* VersionErrorCategory::name () const BOOST_NOEXCEPT {
    return "util-version";
}

std::string VersionErrorCategory::message (int ev) const BOOST_NOEXCEPT {
    switch (Version::Status(ev)) {
#define ITEM(x) case Version::Status::x: return #x;
        ITEM(OK)
        ITEM(LEADING_ZERO)
        ITEM(NEGATIVE_INTEGER)
#undef ITEM
        default: return "(unknown status)";
    }
}

const boost::system::error_category& versionErrorCategory () {
    static VersionErrorCategory instance;
    return instance;
}

boost::system::error_code make_error_code (Version::Status status) {
    return boost::system::error_code(static_cast<int>(status),
        versionErrorCategory());
}

boost::system::error_condition make_error_condition (Version::Status status) {
    return boost::system::error_condition(static_cast<int>(status),
        versionErrorCategory());
}

} // util
