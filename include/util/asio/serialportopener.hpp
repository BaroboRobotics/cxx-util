// Copyright (c) 2014-2016 Barobo, Inc.
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#ifndef UTIL_ASIO_SERIALPORT_HPP
#define UTIL_ASIO_SERIALPORT_HPP

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

class SerialPortOpener {
public:
    explicit SerialPortOpener (boost::asio::io_service& context)
        : mTimer(context)
    {}

    void close (boost::system::error_code& ec) {
        ec = {};
        mTimer.expires_at(boost::asio::steady_timer::time_point::min());
    }

    template <class SettleDelay, class WriteDelay, class CompletionToken>
    auto asyncOpen (boost::asio::serial_port& serialPort,
            const std::string& path, unsigned baudRate,
            SettleDelay&& settleDelay, WriteDelay&& writeDelay, CompletionToken&& token);

    template <class RetryDelay, class SettleDelay, class WriteDelay, class CompletionToken>
    auto asyncOpenUntilSuccess (boost::asio::serial_port& serialPort,
            const std::string& path, unsigned baudRate,
            RetryDelay&& retryDelay, SettleDelay&& settleDelay, WriteDelay&& writeDelay,
            CompletionToken&& token);

private:
    boost::asio::steady_timer mTimer;
};

template <class SettleDelay, class WriteDelay, class CompletionToken>
inline auto SerialPortOpener::asyncOpen (boost::asio::serial_port& serialPort,
        const std::string& path, unsigned baudRate,
        SettleDelay&& settleDelay, WriteDelay&& writeDelay, CompletionToken&& token) {
    mTimer.expires_from_now(std::chrono::seconds(0));
    // If this opener was closed before, `mTimer` might still be at `time_point::min()`.

    auto coroutine =
    [ this
    , &serialPort
    , path
    , baudRate
    , settleDelay = std::forward<SettleDelay>(settleDelay)
    , writeDelay = std::forward<WriteDelay>(writeDelay)
    ]
    (auto&& op, boost::system::error_code ec = {}) {
        reenter (op) {
            serialPort.open(path, ec);
            if (ec) { op.complete(ec); return; }

            if (mTimer.expires_at() == boost::asio::steady_timer::time_point::min()) {
                BOOST_LOG(op.log()) << "asyncOpen cancelled before settle delay";
                return;
            }

            mTimer.expires_from_now(settleDelay);
            yield mTimer.async_wait(std::move(op));

            BOOST_LOG(op.log()) << "Setting baud rate to " << baudRate;
            util::asio::setSerialPortOptions(serialPort, baudRate, ec, op.log());
            if (ec) { op.complete(ec); return; }

            if (mTimer.expires_at() == boost::asio::steady_timer::time_point::min()) {
                BOOST_LOG(op.log()) << "asyncOpen cancelled before write delay";
                return;
            }
            mTimer.expires_from_now(writeDelay);
            yield mTimer.async_wait(std::move(op));
            op.complete(ec);
        }
    };

    return util::asio::asyncDispatch(
        mTimer.get_io_service(),
        std::make_tuple(make_error_code(boost::asio::error::operation_aborted)),
        std::move(coroutine),
        std::forward<CompletionToken>(token)
    );
}

template <class RetryDelay, class SettleDelay, class WriteDelay, class CompletionToken>
inline auto SerialPortOpener::asyncOpenUntilSuccess (boost::asio::serial_port& serialPort,
        const std::string& path, unsigned baudRate,
        RetryDelay&& retryDelay, SettleDelay&& settleDelay, WriteDelay&& writeDelay,
        CompletionToken&& token) {
    auto coroutine =
    [ this
    , &serialPort
    , path
    , baudRate
    , retryDelay = std::forward<RetryDelay>(retryDelay)
    , settleDelay = std::forward<SettleDelay>(settleDelay)
    , writeDelay = std::forward<WriteDelay>(writeDelay)
    ]
    (auto&& op, boost::system::error_code ec = {}) {
        reenter (op) {
            yield this->asyncOpen(
                serialPort, path, baudRate,
                settleDelay, writeDelay, std::move(op)
            );
            while (ec) {
                BOOST_LOG(op.log()) << "asyncOpen: " << ec.message();
                if (mTimer.expires_at() == boost::asio::steady_timer::time_point::min()) {
                    return;
                }
                mTimer.expires_from_now(retryDelay);
                yield mTimer.async_wait(std::move(op));
                if (ec) { op.complete(ec); return; }

                yield this->asyncOpen(
                    serialPort, path, baudRate,
                    settleDelay, writeDelay, std::move(op)
                );
            }
        }
    };

    return util::asio::asyncDispatch(
        mTimer.get_io_service(),
        std::make_tuple(make_error_code(boost::asio::error::operation_aborted)),
        std::move(coroutine),
        std::forward<CompletionToken>(token)
    );
}

}} // util::asio

#include <boost/asio/unyield.hpp>

#endif
