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

#include <boost/fusion/adapted.hpp>
#include <boost/fusion/adapted/std_tuple.hpp>
#include <boost/fusion/adapted/boost_array.hpp>

namespace util {

namespace {

namespace qi = boost::spirit::qi;

using DataType = std::tuple<Version::NumberContainer, boost::optional<Version::IdentifierContainer>, boost::optional<Version::IdentifierContainer>>;

template <class Iter>
struct VersionGrammar : qi::grammar<Iter, DataType()> {
    qi::rule<Iter, DataType()> start;
    qi::rule<Iter, Version::NumberContainer()> numbers;
    qi::rule<Iter, Version::IdentifierContainer()> preRelease;
    qi::rule<Iter, Version::IdentifierContainer()> buildMetadata;
    qi::rule<Iter, Version::IdentifierContainer::value_type()> identifier;

    VersionGrammar () : VersionGrammar::base_type(start, "version") {
        using qi::_1;
        using qi::_val;
        using boost::phoenix::at_c;

        start.name("start");
        start %= -qi::lit('v') > numbers > -preRelease > -buildMetadata > qi::eoi;

        numbers.name("numbers");
        numbers %= qi::uint_ % '.';

        preRelease.name("preRelease");
        preRelease %= '-' > identifier % '.';

        buildMetadata.name("buildMetadata");
        buildMetadata %= '+' > identifier % '.';

        identifier.name("identifier");
        identifier %= +(qi::alnum | '-');

        using ErrorHandlerArgs = boost::fusion::vector<
            Iter&, const Iter&, const Iter&, const qi::info&>;

        auto logError = [](ErrorHandlerArgs args, auto&, qi::error_handler_result&) {
            std::cerr << "Expected '" << at_c<3>(args) << "' here: '"
                << std::string(at_c<2>(args), at_c<1>(args)) << "'\n" << std::endl;
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
#if 0
    auto g = -qi::lit('v')
        >> qi::uint_ % '.'
        >> -((qi::alnum + '-') % '.')
        >> -((qi::alnum + '-') % '.')
        > qi::eoi;
    return qi::parse(v.begin(), v.end(), grammar, std::tie(mNumbers, mPreRelease, mBuildMetadata));
#endif
    VersionGrammar<decltype(v.begin())> grammar;
    return qi::parse(v.begin(), v.end(), grammar, mData);
}

} // util
