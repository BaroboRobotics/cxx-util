// Copyright (c) 2014-2016 Barobo, Inc.
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#ifndef UTIL_ASIO_SETSERIALPORTOPTIONS_HPP
#define UTIL_ASIO_SETSERIALPORTOPTIONS_HPP

#include <util/log.hpp>

#include <boost/asio/io_service.hpp>
#include <boost/asio/serial_port.hpp>
#include <boost/asio/steady_timer.hpp>

#include <boost/system/error_code.hpp>
#include <boost/system/system_error.hpp>

#include <boost/predef.h>

#include <iostream>
#include <chrono>
#include <memory>

#ifdef __MACH__
#include <unistd.h>
#endif

namespace util { namespace asio {

// How long we pause after opening the dongle's device path before setting the
// serial line options. Mac serial ports require some strategic timing
// ninjitsu in order to work, adjust this value as necessary.
static const std::chrono::milliseconds kSerialSettleTimeAfterOpen { 500 };

// How many times we should try setting a serial option before throwing an
// error. On OS X 10.11, it sometimes requires dozens of attempts to set serial
// line options. Setting this to zero is the same as setting it to one.
static const int kMaxSerialSetOptionAttempts { 100 };

template <class Option>
void tenaciousSetOption (boost::asio::serial_port& sp, Option value, const int maxAttempts,
        util::log::Logger lg) {
    auto attempts = 0;
    auto ec = boost::system::error_code{};
    do {
        ec = boost::system::error_code{};
        // On Mac OSX 10.11, we have to flush the serial port before and after
        // set_option(). Otherwise, garbage bytes get sent to the serial port
        // during set_option() which interfere with the Linkbot bootloader.
        // EDIT: We have found the emitting a log message seems to fix this
        // issue more absolutely than using tcflush(). Why? Who knows.
#if BOOST_OS_MACOS
        BOOST_LOG(lg) << "Setting serial port option...";
        //tcflush(sp.lowest_layer().native_handle(), TCIOFLUSH);
#endif
        sp.set_option(value, ec);
#if BOOST_OS_MACOS
        //tcflush(sp.lowest_layer().native_handle(), TCIOFLUSH);
#endif
    } while (maxAttempts > 0
             && ++attempts != maxAttempts
             && ec);
    if (attempts > 1) {
        BOOST_LOG(lg) << "set serial option after " << attempts << " attempts";
    }
    if (ec) {
        throw boost::system::system_error(ec);
    }
}

inline void setSerialPortOptions (boost::asio::serial_port& sp, int baud,
        util::log::Logger lg = {}) {
    using Option = boost::asio::serial_port_base;
    const auto max = kMaxSerialSetOptionAttempts;
    tenaciousSetOption(sp, Option::baud_rate(baud), max, lg);
    tenaciousSetOption(sp, Option::character_size(8), max, lg);
    tenaciousSetOption(sp, Option::parity(Option::parity::none), max, lg);
    tenaciousSetOption(sp, Option::stop_bits(Option::stop_bits::one), max, lg);
    tenaciousSetOption(sp, Option::flow_control(Option::flow_control::none), max, lg);

#if BOOST_OS_UNIX
    // Asio's serial_port class does not appear to modify the VMIN setting of the serial port,
    // which means we inherit whatever setting is already present on the device. If VMIN is 0,
    // a common setting for serial libraries (e.g., PySerial) which perform non-blocking I/O,
    // then all reads will fail with EOF unless there is data present in the input buffer. This
    // is not desirable.
    struct termios tio;
    if (-1 == tcgetattr(sp.lowest_layer().native_handle(), &tio)) {
        BOOST_LOG(lg) << "tcgetattr: "
            << boost::system::error_code(errno, boost::system::generic_category()).message();
    }
    // VMIN and VTIME work together, and are only enabled with ICANON off.
    tio.c_lflag &= ~ICANON;
    tio.c_cc[VMIN] = 1;
    tio.c_cc[VTIME] = 0;
    if (-1 == tcflush(sp.lowest_layer().native_handle(), TCIOFLUSH)) {
        BOOST_LOG(lg) << "tcflush: "
            << boost::system::error_code(errno, boost::system::generic_category()).message();
    }
    if (-1 == tcsetattr(sp.lowest_layer().native_handle(), TCSANOW, &tio)) {
        BOOST_LOG(lg) << "tcsetattr: "
            << boost::system::error_code(errno, boost::system::generic_category()).message();
    }
#endif

#if BOOST_OS_MACOS
    auto handle = sp.native_handle();
    ::write(handle, nullptr, 0);
#endif
}

inline void setSerialPortOptions (boost::asio::serial_port& sp, int baud, boost::system::error_code& ec, util::log::Logger lg = {}) {
    try {
        setSerialPortOptions(sp, baud, lg);
    }
    catch (boost::system::system_error& e) {
        ec = e.code();
    }
}

}} // namespace util::asio

#endif
