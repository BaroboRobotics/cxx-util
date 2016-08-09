// Copyright (c) 2014-2016 Barobo, Inc.
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#ifndef UTIL_ASIO_IOTHREAD_HPP
#define UTIL_ASIO_IOTHREAD_HPP

#include <boost/asio/io_service.hpp>

#include <boost/optional.hpp>

#include <future>

namespace util { namespace asio {

class IoThread {
public:
    IoThread ();
    ~IoThread ();

    size_t join ();

    boost::asio::io_service& context () {
        return mContext;
    }

private:
    boost::asio::io_service mContext;
    boost::optional<boost::asio::io_service::work> mWork;

    std::future<size_t> mJoin;
};

}} // namespace util::asio

#endif
