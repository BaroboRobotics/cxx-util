#include <util/asio/iothread.hpp>

#include <boost/utility/in_place_factory.hpp>

#include <exception>
#include <mutex>
#include <string>

#include <cstdlib>

namespace util {

IoThread::IoThread ()
    : mWork(boost::in_place(std::ref(mContext)))
    // Run the io_service in a separate thread
    , mJoin(std::async(std::launch::async, [this] () { return mContext.run(); }))
{
}

IoThread::~IoThread () {
    // If this throws, you have a bug in your program. Call join() before the
    // destructor, catch the exception, and figure out what's wrong.
    (void)join();
}

size_t IoThread::join () {
    mWork = boost::none;
    return mJoin.valid() ? mJoin.get() : 0;
}

std::shared_ptr<IoThread> IoThread::getGlobal () {
    static std::mutex mutex;
    std::lock_guard<std::mutex> lock {mutex};

    static auto wp = std::weak_ptr<IoThread>{};
    auto p = wp.lock();
    if (!p) {
        p = std::make_shared<IoThread>();
        wp = p;
    }
    return p;
}

} // namespace util
