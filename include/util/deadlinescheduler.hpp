#ifndef UTIL_DEADLINESCHEDULER_HPP
#define UTIL_DEADLINESCHEDULER_HPP

#include <boost/asio/io_service.hpp>
#include <boost/asio/steady_timer.hpp>

#include <iostream>
#include <list>
#include <mutex>
#include <thread>

namespace util {

class DeadlineScheduler {
public:
    DeadlineScheduler () : mWork(mIoService) {
        start();
    }

    ~DeadlineScheduler () {
        stop();
    }

    template <class Duration, class NullaryFunction>
    void executeAfter (Duration duration, NullaryFunction userTask) {
        typename decltype(mTimers)::iterator iter;
        {
            std::lock_guard<std::mutex> emplacementLock { mTimersMutex };
            iter = mTimers.emplace(mTimers.end(), mIoService, duration);
        }
        // Run this lambda after the specified duration.
        iter->async_wait(
            [=] (const boost::system::error_code& error) mutable {
                {
                    std::lock_guard<std::mutex> erasureLock { mTimersMutex };
                    mTimers.erase(iter);
                }
                if (!error) {
                    userTask();
                }
                else {
                    std::cout << "DeadlineScheduler: io_service passed error to task: "
                              << error.message() << '\n';
                }
            }
        );
    }

    void threadMain () {
        boost::system::error_code error;
        auto nTasksCompleted = mIoService.run(error);
        if (error) {
            std::cout << "DeadlineScheduler: io_service::run exited with error: "
                      << error.message() << '\n';
        }
        //std::cout << "DeadlineScheduler completed " << nTasksCompleted
        //          << " of " << nTasksCompleted + mTimers.size() << " tasks.\n";
    }

private:
    void start () {
        std::lock_guard<std::mutex> lock { mIoThreadMutex };
        if (!mIoThread.joinable()) {
            mIoService.reset();
            std::thread t { &DeadlineScheduler::threadMain, this };
            mIoThread.swap(t);
        }
    }

    void stop () {
        std::lock_guard<std::mutex> lock { mIoThreadMutex };
        if (mIoThread.joinable()) {
            mIoService.stop();
            mIoThread.join();
        }
    }

    boost::asio::io_service mIoService;
    boost::asio::io_service::work mWork;

    // Timers will have a reference to mIoService, so they must be destroyed
    // before mIoService.
    std::list<boost::asio::steady_timer> mTimers;
    std::mutex mTimersMutex;

    std::thread mIoThread;
    std::mutex mIoThreadMutex;
};

} // namespace util

#endif
