#ifndef UTIL_ASIO_SETSERIALPORTOPTIONS_HPP
#define UTIL_ASIO_SETSERIALPORTOPTIONS_HPP

#include <boost/asio/io_service.hpp>
#include <boost/asio/serial_port.hpp>
#include <boost/asio/steady_timer.hpp>

#include <boost/log/sources/logger.hpp>
#include <boost/log/sources/record_ostream.hpp>

#include <boost/system/error_code.hpp>
#include <boost/system/system_error.hpp>

#include <iostream>
#include <chrono>
#include <memory>

#ifdef __MACH__
#include <unistd.h>
#endif

namespace util {

// How long we pause after opening the dongle's device path before setting the
// serial line options. Mac serial ports require some strategic timing
// ninjitsu in order to work, adjust this value as necessary.
static const std::chrono::milliseconds kSerialSettleTimeAfterOpen { 500 };

// How many times we should try setting a serial option before throwing an
// error. On OS X 10.11, it sometimes requires dozens of attempts to set serial
// line options. Setting this to zero is the same as setting it to one.
static const int kMaxSerialSetOptionAttempts { 100 };

template <class Option>
void tenaciousSetOption (boost::asio::serial_port& sp, Option value, const int maxAttempts) {
    auto attempts = 0;
    auto ec = boost::system::error_code{};
    boost::log::sources::logger lg;
    do {
        ec = boost::system::error_code{};
        // On Mac OSX 10.11, we have to flush the serial port before and after
        // set_option(). Otherwise, garbage bytes get sent to the serial port
        // during set_option() which interfere with the Linkbot bootloader.
        // EDIT: We have found the emitting a log message seems to fix this
        // issue more absolutely than using tcflush(). Why? Who knows.
        #ifdef __MACH__
        BOOST_LOG(lg) << "Setting serial port option...";
        //tcflush(sp.lowest_layer().native_handle(), TCIOFLUSH);
        #endif
        sp.set_option(value, ec);
        #ifdef __MACH__
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

inline void setSerialPortOptions (boost::asio::serial_port& sp, int baud) {
    using Option = boost::asio::serial_port_base;
    const auto max = kMaxSerialSetOptionAttempts;
    tenaciousSetOption(sp, Option::baud_rate(baud), max);
    tenaciousSetOption(sp, Option::character_size(8), max);
    tenaciousSetOption(sp, Option::parity(Option::parity::none), max);
    tenaciousSetOption(sp, Option::stop_bits(Option::stop_bits::one), max);
    tenaciousSetOption(sp, Option::flow_control(Option::flow_control::none), max);
#ifdef __MACH__
    auto handle = sp.native_handle();
    ::write(handle, nullptr, 0);
#endif
}

inline void setSerialPortOptions (boost::asio::serial_port& sp, int baud, boost::system::error_code& ec) {
    try {
        setSerialPortOptions(sp, baud);
    }
    catch (boost::system::system_error& e) {
        ec = e.code();
    }
}

} // namespace util

#endif
