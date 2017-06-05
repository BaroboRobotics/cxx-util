// Copyright (c) 2014-2016 Barobo, Inc.
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#ifndef UTIL_ASIO_SERIALPORT_HPP
#define UTIL_ASIO_SERIALPORT_HPP

#include <util/asio/setserialportoptions.hpp>
#include <util/asio/operation.hpp>
#include <util/log.hpp>

#include <boost/asio/io_service.hpp>
#include <boost/asio/serial_port.hpp>
#include <boost/asio/steady_timer.hpp>

#include <chrono>
#include <string>
#include <utility>

#include <boost/asio/yield.hpp>

namespace util { namespace asio {

// =======================================================================================
// Set baud rate op

template <class Handler = void(boost::system::error_code)>
struct SetBaudRateOp: boost::asio::coroutine {
    using handler_type = Handler;
    using allocator_type = beast::handler_alloc<char, handler_type>;
    using executor_type = composed::handler_executor<handler_type>;

    boost::asio::steady_timer& timer;
    boost::asio::serial_port& stream;
    unsigned baudRate;
    boost::asio::steady_timer::duration writeDelay;

    composed::associated_logger_t<handler_type> lg;
    boost::system::error_code ec;

    template <class WriteDelay>
    SetBaudRateOp(handler_type& h, boost::asio::steady_timer& t, boost::asio::serial_port& s,
                  unsigned br, WriteDelay&& wd)
        : timer(t)
        , stream(s)
        , baudRate(br)
        , writeDelay(std::forward<WriteDelay>(wd))
        , lg(composed::get_associated_logger(h))
    {}

    void operator()(composed::op<SetBaudRateOp>&);
};

constexpr composed::operation<SetBaudRateOp<>> asyncSetBaudRate;
// Set the baud rate on a serial_port, then wait an amount of time before completing.
// To initiate:
//   asyncSetBaudRate(timer, serialDevice, baudRate, writeDelay, handler);
//
// To cancel:
//   timer.expires_at(boost::asio::steady_timer::time_point::min());

template <class Handler>
void SetBaudRateOp<Handler>::operator()(composed::op<SetBaudRateOp>& op) {
    if (!ec) reenter(this) {
        BOOST_LOG(lg) << "Setting baud rate to " << baudRate;
        util::asio::setSerialPortOptions(stream, baudRate, ec);
        if (ec) { yield return timer.get_io_service().post(op()); }

        if (timer.expires_at() == boost::asio::steady_timer::time_point::min()) {
            ec = boost::asio::error::operation_aborted;
            timer.expires_from_now(std::chrono::seconds(0));
            yield return timer.get_io_service().post(op());
        }

        timer.expires_from_now(writeDelay);
        yield return timer.async_wait(op(ec));

    }
    op.complete(ec);
};

// =======================================================================================
// Open op

template <class Handler = void(boost::system::error_code)>
struct OpenOp: boost::asio::coroutine {
    using handler_type = Handler;
    using allocator_type = beast::handler_alloc<char, handler_type>;
    using executor_type = composed::handler_executor<handler_type>;

    boost::asio::steady_timer& timer;
    boost::asio::serial_port& stream;
    std::string path;
    boost::asio::steady_timer::duration settleDelay;

    composed::associated_logger_t<handler_type> lg;
    boost::system::error_code ec;

    template <class SettleDelay>
    OpenOp(handler_type& h, boost::asio::steady_timer& t, boost::asio::serial_port& s,
                  const std::string& p, SettleDelay&& sd)
        : timer(t)
        , stream(s)
        , path(p)
        , settleDelay(std::forward<SettleDelay>(sd))
        , lg(composed::get_associated_logger(h))
    {}

    void operator()(composed::op<OpenOp>&);
};

constexpr composed::operation<OpenOp<>> asyncOpen;
// Open a serial_port, then wait an amount of time before completing.
// To initiate:
//   asyncOpen(timer, serialDevice, devicePath, settleDelay, handler);
//
// To cancel:
//   timer.expires_at(boost::asio::steady_timer::time_point::min());

template <class Handler>
void OpenOp<Handler>::operator()(composed::op<OpenOp>& op) {
    if (!ec) reenter(this) {
        stream.open(path, ec);
        if (ec) { yield return timer.get_io_service().post(op()); }

        if (timer.expires_at() == boost::asio::steady_timer::time_point::min()) {
            ec = boost::asio::error::operation_aborted;
            timer.expires_from_now(std::chrono::seconds(0));
            yield return timer.get_io_service().post(op());
        }

        timer.expires_from_now(settleDelay);
        yield return timer.async_wait(op(ec));
    }
    op.complete(ec);
};

// =======================================================================================
// Open and set baud rate loop op

template <class Handler = void(boost::system::error_code)>
struct OpenAndSetBaudRateLoop: boost::asio::coroutine {
    using handler_type = Handler;
    using allocator_type = beast::handler_alloc<char, handler_type>;
    using executor_type = composed::handler_executor<handler_type>;

    boost::asio::steady_timer& timer;
    boost::asio::serial_port& stream;
    std::string path;
    unsigned baudRate;
    boost::asio::steady_timer::duration retryDelay;
    boost::asio::steady_timer::duration settleDelay;
    boost::asio::steady_timer::duration writeDelay;

    composed::associated_logger_t<handler_type> lg;
    boost::system::error_code ec;

    template <class RetryDelay, class SettleDelay, class WriteDelay>
    OpenAndSetBaudRateLoop(handler_type& h, boost::asio::steady_timer& t, boost::asio::serial_port& s,
                  const std::string& p, unsigned br,
                  RetryDelay&& rd, SettleDelay&& sd, WriteDelay&& wd)
        : timer(t)
        , stream(s)
        , path(p)
        , baudRate(br)
        , retryDelay(std::forward<RetryDelay>(rd))
        , settleDelay(std::forward<SettleDelay>(sd))
        , writeDelay(std::forward<WriteDelay>(wd))
        , lg(composed::get_associated_logger(h))
    {}

    void operator()(composed::op<OpenAndSetBaudRateLoop>&);
};

constexpr composed::operation<OpenAndSetBaudRateLoop<>> asyncOpenAndSetBaudRateLoop;
// Loop forever trying to open a serial_port and set its baud rate.
// To initiate:
//   asyncOpenAndSetBaudRateLoop(timer, serialDevice, devicePath, baudRate,
//                               retryDelay, settleDelay, writeDelay, handler);
//
// To cancel:
//   timer.expires_at(boost::asio::steady_timer::time_point::min());

template <class Handler>
void OpenAndSetBaudRateLoop<Handler>::operator()(composed::op<OpenAndSetBaudRateLoop>& op) {
    reenter(this) {
        do {
            yield return asyncOpen(timer, stream, path, settleDelay, op(ec));

            if (!ec) {
                if (timer.expires_at() == boost::asio::steady_timer::time_point::min()) {
                    ec = boost::asio::error::operation_aborted;
                    timer.expires_from_now(std::chrono::seconds(0));
                    yield return timer.get_io_service().post(op());
                }

                yield return asyncSetBaudRate(timer, stream, baudRate, writeDelay, op(ec));
            }

            if (ec) {
                if (timer.expires_at() == boost::asio::steady_timer::time_point::min()) {
                    ec = boost::asio::error::operation_aborted;
                    timer.expires_from_now(std::chrono::seconds(0));
                    yield return timer.get_io_service().post(op());
                }

                BOOST_LOG(lg) << "open / set baud rate error: " << ec.message();
                timer.expires_from_now(retryDelay);
                yield return timer.async_wait(op(ec));
            }
        } while (ec);
    }
    op.complete(ec);
};

}} // util::asio

#include <boost/asio/unyield.hpp>

#endif
