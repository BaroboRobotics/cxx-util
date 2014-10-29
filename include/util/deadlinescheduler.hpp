#ifndef UTIL_DEADLINESCHEDULER_HPP
#define UTIL_DEADLINESCHEDULER_HPP

#include <boost/asio/io_service.hpp>
#include <boost/asio/steady_timer.hpp>

#include <boost/log/common.hpp>
#include <boost/log/sources/logger.hpp>

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
        BOOST_LOG_NAMED_SCOPE("util::DeadlineScheduler::executeAfter");
        typename decltype(mTimers)::iterator iter;
        {
            std::lock_guard<std::mutex> emplacementLock { mTimersMutex };
            iter = mTimers.emplace(mTimers.end(), mIoService, duration);
        }
        // Run this lambda after the specified duration.
        iter->async_wait(
            [=] (const boost::system::error_code& error) mutable {
                BOOST_LOG_NAMED_SCOPE("util::DeadlineScheduler::executeAfter::(lambda)");
                {
                    std::lock_guard<std::mutex> erasureLock { mTimersMutex };
                    mTimers.erase(iter);
                }
                if (!error) {
                    userTask();
                }
                else {
                    BOOST_LOG(mLog) << "io_service passed error to task: "
                                    << error.message();
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
        (void)nTasksCompleted;
        //std::cout << "DeadlineScheduler completed " << nTasksCompleted
        //          << " of " << nTasksCompleted + mTimers.size() << " tasks.\n";
    }

private:
    void start () {
        if (!mIoThread.joinable()) {
            mIoService.reset();
            std::thread t { &DeadlineScheduler::threadMain, this };
            mIoThread.swap(t);
        }
    }

    void stop () {
        if (mIoThread.joinable()) {
            mIoService.stop();
            mIoThread.join();
        }
    }

    boost::log::sources::logger_mt mLog;

    boost::asio::io_service mIoService;
    boost::asio::io_service::work mWork;

    // Timers will have a reference to mIoService, so they must be destroyed
    // before mIoService.
    std::list<boost::asio::steady_timer> mTimers;
    std::mutex mTimersMutex;

    std::thread mIoThread;
};

} // namespace util

#endif
