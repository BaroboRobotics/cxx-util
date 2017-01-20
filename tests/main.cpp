// Copyright (c) 2016 Barobo, Inc.
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#define DOCTEST_CONFIG_IMPLEMENT
#include <util/doctest.h>
#include <util/log.hpp>

#include <boost/program_options/parsers.hpp>

int main (int argc, char** argv) {
    // We need a custom main() because we want to make sure we set up logging only once.
    // TODO: integrate log options parsing with doctest's.
    auto desc = util::log::optionsDescription();
    boost::program_options::parsed_options parsed{&desc};
    boost::program_options::variables_map options;
    boost::program_options::store(parsed, options);
    boost::program_options::notify(options);

    doctest::Context context;
    context.applyCommandLine(argc, argv);
    return context.run();
}
