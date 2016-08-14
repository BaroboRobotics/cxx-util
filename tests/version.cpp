// Copyright (c) 2016 Barobo, Inc.
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#include <util/version.hpp>

#include <iostream>

#include <cassert>

using util::Version;

#if NDEBUG
#warning "TODO: Use a testing framework that isn't assert()"
#endif

int main () try {
    {
        // parsing testing

        Version v;
        std::cout << v << '\n';

        assert(v.parse("1"));
        std::cout << v << '\n';
        assert(v.parse("1.9"));
        std::cout << v << '\n';
        assert(v.parse("1.9.0"));
        std::cout << v << '\n';
        assert(v.parse("1.9.0.1"));
        std::cout << v << '\n';
        assert(v.parse("v1.9.0"));
        std::cout << v << '\n';
        assert(v.parse("v1.9.0--"));
        std::cout << v << '\n';
        assert(v.parse("v1.9.0-a.b.2.d----d"));
        std::cout << v << '\n';
        assert(v.parse("v1.9.0+build-metadata.foo.bar.flop.1"));
        std::cout << v << '\n';
        assert(v.parse("v1.9.0-a.b.2.d----d+build-metadata.foo.bar.flop.1"));
        std::cout << v << '\n';

        assert(!v.parse("-1"));
        assert(!v.parse("1.-0"));
        assert(!v.parse("1.9.0-,"));
        assert(!v.parse("1.9.0-alpha(1)"));
    }

    {
        // precedence testing

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

        assert(a == a);
        assert(b == b);
        assert(c == c);
        assert(d == d);
        assert(e == e);
        assert(f == f);
        assert(g == g);
        assert(h == h);
        assert(h == h2);
        assert(h == h3);

        assert(a < b);
        assert(a < c);
        assert(a < d);
        assert(a < e);
        assert(a < f);
        assert(a < g);
        assert(a < h);
        assert(a < h2);
        assert(a < h3);

        assert(b < c);
        assert(b < d);
        assert(b < e);
        assert(b < f);
        assert(b < g);
        assert(b < h);
        assert(b < h2);
        assert(b < h3);

        assert(c < d);
        assert(c < e);
        assert(c < f);
        assert(c < g);
        assert(c < h);
        assert(c < h2);
        assert(c < h3);

        assert(d < e);
        assert(d < f);
        assert(d < g);
        assert(d < h);
        assert(d < h2);
        assert(d < h3);

        assert(e < f);
        assert(e < g);
        assert(e < h);
        assert(e < h2);
        assert(e < h3);

        assert(f < g);
        assert(f < h);
        assert(f < h2);
        assert(f < h3);

        assert(!(h < h2));
        assert(!(h < h3));
        assert(!(h2 < h3));
        assert(!(h > h2));
        assert(!(h > h3));
        assert(!(h2 > h3));

        assert(!(a > b));
        assert(!(a > c));
        assert(!(a > d));
        assert(!(a > e));
        assert(!(a > f));
        assert(!(a > g));
        assert(!(a > h));
        assert(!(a > h2));
        assert(!(a > h3));

        assert(!(b > c));
        assert(!(b > d));
        assert(!(b > e));
        assert(!(b > f));
        assert(!(b > g));
        assert(!(b > h));
        assert(!(b > h2));
        assert(!(b > h3));

        assert(!(c > d));
        assert(!(c > e));
        assert(!(c > f));
        assert(!(c > g));
        assert(!(c > h));
        assert(!(c > h2));
        assert(!(c > h3));

        assert(!(d > e));
        assert(!(d > f));
        assert(!(d > g));
        assert(!(d > h));
        assert(!(d > h2));
        assert(!(d > h3));

        assert(!(e > f));
        assert(!(e > g));
        assert(!(e > h));
        assert(!(e > h2));
        assert(!(e > h3));

        assert(!(f > g));
        assert(!(f > h));
        assert(!(f > h2));
        assert(!(f > h3));

        std::cout << a << '\n';
        std::cout << b << '\n';
        std::cout << c << '\n';
        std::cout << d << '\n';
        std::cout << e << '\n';
        std::cout << f << '\n';
        std::cout << g << '\n';
        std::cout << h << '\n';
        std::cout << h2 << '\n';
        std::cout << h3 << '\n';
    }

    return 0;
}
catch (std::exception& e) {
    std::cout << "Unhandled exception: " << e.what() << '\n';
}
