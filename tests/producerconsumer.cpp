#include <util/producerconsumerqueue.hpp>

#include <random>

#include <iostream>

#include <cassert>

template <class T>
T randomNumberBetween (T a, T b) {
    static std::random_device rd{};
    static auto gen = std::mt19937{rd()};
    static auto dis = std::uniform_int_distribution<T>{a, b};
    return dis(gen);
}

int main () {
    {
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
            std::cout << "Depth " << pcq.depth() << " = " << produces << " - " << consumes << '\n';
            assert(pcq.depth() == (produces - consumes));
        }
    }
}
