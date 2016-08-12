// Copyright (c) 2016 Barobo, Inc.
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#include <util/version.hpp>
#include <util/log.hpp>

#include <boost/spirit/include/phoenix_operator.hpp>
#include <boost/spirit/include/phoenix_fusion.hpp>
#include <boost/spirit/include/phoenix_stl.hpp>
#include <boost/spirit/include/qi.hpp>

#include <boost/spirit/include/karma.hpp>
#include <boost/spirit/include/karma_stream.hpp>

#include <boost/fusion/adapted.hpp>
#include <boost/fusion/adapted/std_tuple.hpp>
#include <boost/fusion/adapted/boost_array.hpp>

namespace util {

namespace {

namespace qi = boost::spirit::qi;
namespace karma = boost::spirit::karma;

using boost::spirit::ascii::char_;
using boost::spirit::auto_;
using boost::spirit::lit;
using boost::spirit::uint_;

using DataType = std::tuple<Version::NumberContainer, boost::optional<Version::PreReleaseContainer>, boost::optional<Version::IdentifierContainer>>;

template <class Iter>
struct VersionGrammar : qi::grammar<Iter, DataType()> {
    qi::rule<Iter, DataType()> start;
    qi::rule<Iter, Version::NumberContainer()> numbers;
    qi::rule<Iter, Version::PreReleaseContainer()> preRelease;
    qi::rule<Iter, Version::IdentifierContainer()> buildMetadata;
    qi::rule<Iter, Version::IdentifierContainer::value_type()> identifier;

    VersionGrammar () : VersionGrammar::base_type(start, "version") {
        start.name("start");
        start %= -lit('v') > numbers > -preRelease > -buildMetadata;

        numbers.name("numbers");
        numbers %= uint_ % '.';

        preRelease.name("preRelease");
        preRelease %= '-' > (uint_ | identifier) % '.';

        buildMetadata.name("buildMetadata");
        buildMetadata %= '+' > identifier % '.';

        identifier.name("identifier");
        identifier %= +char_("0-9A-Za-z-");

        using ErrorHandlerArgs = boost::fusion::vector<
            Iter&, const Iter&, const Iter&, const qi::info&>;

        auto logError = [](ErrorHandlerArgs args, auto&, qi::error_handler_result&) {
#if 0
            using boost::phoenix::at_c;
            std::cerr << "Expected '" << at_c<3>(args) << "' here: '"
                << std::string(at_c<2>(args), at_c<1>(args)) << "'\n" << std::endl;
#endif
        };

        qi::on_error<qi::fail>(start, logError);
        qi::on_error<qi::fail>(numbers, logError);
        qi::on_error<qi::fail>(preRelease, logError);
        qi::on_error<qi::fail>(buildMetadata, logError);
        qi::on_error<qi::fail>(identifier, logError);
    }
};

} // <anonymous>

Version::Version (const std::string& v) {
    if (!parse(v)) {
        throw std::runtime_error{"Version parsing failed"};
    }
}

bool Version::parse (const std::string& v) {
    mData = {};
    VersionGrammar<decltype(v.begin())> grammar;
    return qi::parse(v.begin(), v.end(), grammar >> qi::eoi, mData);
}

bool operator== (const Version& a, const Version& b) {
    return a.numbers() == b.numbers() && a.preRelease() == b.preRelease();
}

bool operator< (const Version& a, const Version& b) {
    if (a.numbers() == b.numbers()) {
        // `none` compares less-than to any non-empty `optional`, which is the opposite semantics
        // of what we want. The logic in the ternary operator corrects false positives/negatives.
        return a.preRelease() < b.preRelease()
            ? a.preRelease() && b.preRelease()
            : a.preRelease() && !b.preRelease();
    }
    else {
        return a.numbers() < b.numbers();
    }
}

std::ostream& operator<< (std::ostream& os, const Version& v) {
    if (v.numbers().size()) {
        os << karma::format(
            uint_ % '.'
            << -('-' << auto_ % '.')
            << -('+' << auto_ % '.')
            , v.numbers(), v.preRelease(), v.buildMetadata());
    }
    return os;
}

} // util
