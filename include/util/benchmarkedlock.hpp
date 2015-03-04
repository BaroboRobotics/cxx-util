#ifndef UTIL_BENCHMARKEDLOCK_HPP
#define UTIL_BENCHMARKEDLOCK_HPP

#include <boost/log/sources/logger.hpp>
#include <boost/log/sources/record_ostream.hpp>

#include <chrono>
#include <memory>
#include <mutex>

namespace util {

class BenchmarkedLock {
public:
    BenchmarkedLock (std::mutex& m)
        : mStart(std::chrono::steady_clock::now())
        , mLock(new std::lock_guard<std::mutex>{m})
        , mAcquired(std::chrono::steady_clock::now())
    {}

    friend void swap (BenchmarkedLock& lhs, BenchmarkedLock& rhs) BOOST_NOEXCEPT {
        using std::swap;
        swap(lhs.mStart, rhs.mStart);
        swap(lhs.mLock, rhs.mLock);
        swap(lhs.mAcquired, rhs.mAcquired);
    }

    BenchmarkedLock (BenchmarkedLock&& that) {
        swap(*this, that);
    }

    BenchmarkedLock& operator= (BenchmarkedLock that) {
        swap(*this, that);
        return *this;
    }

    ~BenchmarkedLock () {
#if 0
        using FpUs = std::chrono::duration<double, std::chrono::microseconds::period>;
        boost::log::sources::logger log;
        BOOST_LOG(log) << "Lock acquired after "
                       << std::chrono::duration_cast<FpUs>(mAcquired - mStart).count()
                       << ", in use for "
                       << std::chrono::duration_cast<FpUs>(std::chrono::steady_clock::now() - mAcquired).count()
                       << " microseconds";
#endif
    }

private:
    std::chrono::steady_clock::time_point mStart;
    std::unique_ptr<std::lock_guard<std::mutex>> mLock;
    std::chrono::steady_clock::time_point mAcquired;
};

}

#endif
