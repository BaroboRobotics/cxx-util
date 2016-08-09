// Copyright (c) 2014-2016 Barobo, Inc.
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#include <util/asio/iothread.hpp>

#include <boost/utility/in_place_factory.hpp>

#include <exception>
#include <string>

#include <cstdlib>

namespace util { namespace asio {

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

}} // namespace util::asio
