// Copyright (c) 2014-2016 Barobo, Inc.
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#include <util/doctest.h>
#include <util/callback.hpp>

struct AddTwo {
    static int exec (int i) { return i + 2; };
};

TEST_CASE("callbacks can call back") {
    int x;
    util::Callback<int(int)> sig;

    auto addOne = [] (int i) { return i + 1; };
    using Lambda = decltype(addOne);

    sig = BIND_MEM_CB(&Lambda::operator(), &addOne);
    x = sig(1);
    CHECK(2 == x);

    sig = BIND_FREE_CB(&AddTwo::exec);
    x = sig(1);
    CHECK(3 == x);

    /* TODO: add some more cases, comment this stuff */
}
