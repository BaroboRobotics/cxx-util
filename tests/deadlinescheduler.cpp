#include "util/deadlinescheduler.hpp"

#include <algorithm>
#include <random>
#include <string>

const int kNTasks = 100;
const int kMillisecondsMin = 50;
const int kMillisecondsMax = 5000;

// Populate a DeadlineScheduler with 1000 tasks scheduled at random delay
// times between 50 and 5000 milliseconds. Run the scheduler. Verify that the
// completed tasks were executed in order.
int main (int argc, char** argv) {
    assert(2 <= argc);

    auto runTimeMs = std::stoi(std::string(argv[1]));
    auto seed = 3 == argc ? std::stoi(std::string(argv[2])) : std::random_device()();

    std::default_random_engine randomEngine { seed };
    std::uniform_int_distribution<> randomMilliseconds {
        kMillisecondsMin, kMillisecondsMax
    };

    std::vector<int> integers;

    util::DeadlineScheduler scheduler;
    for (int i = 0; i < kNTasks; ++i) {
        auto delayMs = randomMilliseconds(randomEngine);
        scheduler.executeAfter(std::chrono::milliseconds(delayMs),
            [delayMs, &integers] () { 
                std::cout << delayMs << " milliseconds\n";
                integers.push_back(delayMs);
            }
        );
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(runTimeMs));

    std::cout << "Seed was " << seed << '\n';
    assert(std::is_sorted(integers.cbegin(), integers.cend()));
}
