// Copyright (c) 2014-2016 Barobo, Inc.
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#include <util/doctest.h>
#include <util/producerconsumerqueue.hpp>

#include <random>

template <class T>
T randomNumberBetween (T a, T b) {
    static std::random_device rd{};
    static auto gen = std::mt19937{rd()};
    static auto dis = std::uniform_int_distribution<T>{a, b};
    return dis(gen);
}

TEST_CASE("ProducerConsumerQueue") {
    util::ProducerConsumerQueue<> pcq;
    auto produces = 0;
    auto consumes = 0;
    for (auto i = 0; i < 1000; ++i) {
        auto n = randomNumberBetween(0, 1);
        if (n) {
            pcq.produce();
            ++produces;
        }
        else {
            pcq.consume([]{});
            ++consumes;
        }
        CHECK(pcq.depth() == (produces - consumes));
    }
}
