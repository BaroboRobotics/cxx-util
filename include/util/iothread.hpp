#ifndef UTIL_IOTHREAD_HPP
#define UTIL_IOTHREAD_HPP

#include <boost/asio/io_service.hpp>

#include <boost/optional.hpp>

#include <future>
#include <memory>

namespace util {

class IoThread {
public:
    IoThread ();
    ~IoThread ();

    static std::shared_ptr<IoThread> getGlobal ();

    size_t join ();

    boost::asio::io_service& context () {
        return mContext;
    }

private:
    boost::asio::io_service mContext;
    boost::optional<boost::asio::io_service::work> mWork;

    std::future<size_t> mJoin;
};

} // namespace baromesh

#endif