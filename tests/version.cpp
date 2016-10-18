// Copyright (c) 2016 Barobo, Inc.
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#include <util/doctest.h>
#include <util/version.hpp>

using util::Version;

TEST_CASE("Version can parse semantic version strings") {
    Version v;

    CHECK(v.parse("1"));
    CHECK(v.parse("1.9"));
    CHECK(v.parse("1.9.0"));
    CHECK(v.parse("1.9.0.1"));
    CHECK(v.parse("v1.9.0"));
    CHECK(v.parse("v1.9.0--"));
    CHECK(v.parse("v1.9.0-a.b.2.d----d"));
    CHECK(v.parse("v1.9.0+build-metadata.foo.bar.flop.1"));
    CHECK(v.parse("v1.9.0-a.b.2.d----d+build-metadata.foo.bar.flop.1"));

    CHECK(!v.parse("-1"));
    CHECK(!v.parse("1.-0"));
    CHECK(!v.parse("1.9.0-,"));
    CHECK(!v.parse("1.9.0-alpha(1)"));
}

TEST_CASE("Version implements semantic version precedence") {
    auto a = Version{"1.9.0"};
    auto b = Version{"1.10.0-0.2.0"};
    auto c = Version{"1.10.0-0.10.0"};
    auto d = Version{"1.10.0-alpha.0"};
    auto e = Version{"1.10.0-alpha.1"};
    auto f = Version{"1.10.0-alpha.1.0"};
    auto g = Version{"1.10.0-dev"};
    auto h = Version{"1.10.0"};
    auto h2 = Version{"1.10.0+build.0"};
    auto h3 = Version{"1.10.0+build.1"};

    CHECK(a == a);
    CHECK(b == b);
    CHECK(c == c);
    CHECK(d == d);
    CHECK(e == e);
    CHECK(f == f);
    CHECK(g == g);
    CHECK(h == h);
    CHECK(h == h2);
    CHECK(h == h3);

    CHECK(a < b);
    CHECK(a < c);
    CHECK(a < d);
    CHECK(a < e);
    CHECK(a < f);
    CHECK(a < g);
    CHECK(a < h);
    CHECK(a < h2);
    CHECK(a < h3);

    CHECK(b < c);
    CHECK(b < d);
    CHECK(b < e);
    CHECK(b < f);
    CHECK(b < g);
    CHECK(b < h);
    CHECK(b < h2);
    CHECK(b < h3);

    CHECK(c < d);
    CHECK(c < e);
    CHECK(c < f);
    CHECK(c < g);
    CHECK(c < h);
    CHECK(c < h2);
    CHECK(c < h3);

    CHECK(d < e);
    CHECK(d < f);
    CHECK(d < g);
    CHECK(d < h);
    CHECK(d < h2);
    CHECK(d < h3);

    CHECK(e < f);
    CHECK(e < g);
    CHECK(e < h);
    CHECK(e < h2);
    CHECK(e < h3);

    CHECK(f < g);
    CHECK(f < h);
    CHECK(f < h2);
    CHECK(f < h3);

    CHECK(!(h < h2));
    CHECK(!(h < h3));
    CHECK(!(h2 < h3));
    CHECK(!(h > h2));
    CHECK(!(h > h3));
    CHECK(!(h2 > h3));

    CHECK(!(a > b));
    CHECK(!(a > c));
    CHECK(!(a > d));
    CHECK(!(a > e));
    CHECK(!(a > f));
    CHECK(!(a > g));
    CHECK(!(a > h));
    CHECK(!(a > h2));
    CHECK(!(a > h3));

    CHECK(!(b > c));
    CHECK(!(b > d));
    CHECK(!(b > e));
    CHECK(!(b > f));
    CHECK(!(b > g));
    CHECK(!(b > h));
    CHECK(!(b > h2));
    CHECK(!(b > h3));

    CHECK(!(c > d));
    CHECK(!(c > e));
    CHECK(!(c > f));
    CHECK(!(c > g));
    CHECK(!(c > h));
    CHECK(!(c > h2));
    CHECK(!(c > h3));

    CHECK(!(d > e));
    CHECK(!(d > f));
    CHECK(!(d > g));
    CHECK(!(d > h));
    CHECK(!(d > h2));
    CHECK(!(d > h3));

    CHECK(!(e > f));
    CHECK(!(e > g));
    CHECK(!(e > h));
    CHECK(!(e > h2));
    CHECK(!(e > h3));

    CHECK(!(f > g));
    CHECK(!(f > h));
    CHECK(!(f > h2));
    CHECK(!(f > h3));
}
