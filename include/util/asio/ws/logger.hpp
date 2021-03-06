// Copyright (c) 2014-2016 Barobo, Inc.
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#ifndef UTIL_ASIO_WS_LOGGER_HPP
#define UTIL_ASIO_WS_LOGGER_HPP

#include <util/log.hpp>

#include <boost/log/attributes/constant.hpp>

#include <websocketpp/logger/basic.hpp>
#include <websocketpp/logger/levels.hpp>

#include <ctime>
#include <iostream>
#include <iomanip>
#include <string>

namespace util { namespace asio { namespace ws {

template <typename Concurrency, typename Names>
class Logger : public ::websocketpp::log::basic<Concurrency, Names> {
public:
    typedef ::websocketpp::log::basic<Concurrency, Names> base;
    using channel_type_hint = ::websocketpp::log::channel_type_hint;
    using level = ::websocketpp::log::level;

    Logger (channel_type_hint::value hint = channel_type_hint::access)
        : base(hint)
    {
        initSource();
    }

    Logger (level channels,
        channel_type_hint::value hint = channel_type_hint::access)
        : base(channels, hint)
    {
        initSource();
    }

    void write (level channel, const std::string& msg) {
        write(channel, msg.c_str());
    }

    void write(level channel, char const* msg) {
        scoped_lock_type lock(base::m_lock);
        if (!this->dynamic_test(channel)) { return; }
        BOOST_LOG(mLog) << "[" << Names::channel_name(channel) << "] " << msg;
    }

private:
    void initSource () {
        mLog.add_attribute("Protocol", boost::log::attributes::constant<std::string>("WS++"));
    }

    typedef typename base::scoped_lock_type scoped_lock_type;
    util::log::Logger mLog;
};

}}} // namespace util::asio::ws

#endif // BAROMESH_WEBSOCKETLOGGER_HPP
