// Copyright (c) 2014-2016 Barobo, Inc.
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#include <util/doctest.h>
#include <util/variant.hpp>

#include <memory>

struct A {
    int id;
};

struct B {
    int id;
};

template <class T>
struct AssertIdEquals {
    AssertIdEquals (int id) : id(id) { }
    void operator() (T& x) const { CHECK(x.id == id); }
    template <class U>
    void operator() (U& x) const { CHECK(false); }
    int id;
};

TEST_CASE("Variant") {
    using Var = util::Variant<A, B>;
    {
        // Not DefaultConstructible
        //Var v;
        //Var u { };
    }

    {
        // const and reference types won't work, these should static assert
        //util::Variant<const A, B> v { B() };
        //util::Variant<A, B&> u { A() };
    }

    {
        // unbounded types won't work
        //Var v { int() };
    }

    {
        // const Variants are fine, though
        const Var v { A() };
        // You just can't assign to them after initialization.
        //v = B();
    }

    {
        // MoveConstructible, syntax 1
        Var v = A{0};
        util::apply(AssertIdEquals<A>(0), v);

        // MoveAssignable
        static_assert(std::is_same<decltype(v = B{2}), Var&>::value,
                "return type of assignment operator is wrong");
        v = B{2};
        util::apply(AssertIdEquals<B>(2), v);

        v = B{3};
        util::apply(AssertIdEquals<B>(3), v);

        // MoveConstructible, syntax 2
        std::unique_ptr<Var> u { new Var(B{1}) };
        util::apply(AssertIdEquals<B>(1), *u);
    }
}
