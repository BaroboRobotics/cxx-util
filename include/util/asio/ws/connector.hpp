// Copyright (c) 2014-2016 Barobo, Inc.
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#ifndef UTIL_ASIO_WS_CONNECTOR_HPP
#define UTIL_ASIO_WS_CONNECTOR_HPP

#include <util/log.hpp>

#include <util/asio/asynccompletion.hpp>
#include <util/asio/transparentservice.hpp>

#include <util/asio/ws/logger.hpp>
#include <util/asio/ws/messagequeue.hpp>

#include <websocketpp/client.hpp>
#include <websocketpp/close.hpp>
#include <websocketpp/config/asio_no_tls_client.hpp>

#include <tuple>
#include <utility>

namespace util { namespace asio { namespace ws {

class ConnectorImpl : public std::enable_shared_from_this<ConnectorImpl> {
public:
    // Client config with asio transport, TLS disabled, and Boost.Log logging
    struct Config : public ::websocketpp::config::asio_client {
        typedef Config type;
        typedef ::websocketpp::config::asio_client base;

        typedef Logger<base::concurrency_type, ::websocketpp::log::elevel> elog_type;
        typedef Logger<base::concurrency_type, ::websocketpp::log::alevel> alog_type;

        struct transport_config : public base::transport_config {
            typedef type::alog_type alog_type;
            typedef type::elog_type elog_type;
        };

        typedef ::websocketpp::transport::asio::endpoint<transport_config> transport_type;
    };

    using Connection = ::websocketpp::connection<Config>;
    using ConnectionPtr = Connection::ptr;
    using MessageQueue = ::util::asio::ws::MessageQueue<Config>;

    explicit ConnectorImpl (boost::asio::io_service& context)
        : mContext(context)
    {
        mWsClient.init_asio(&mContext);
        mWsClient.set_access_channels(::websocketpp::log::alevel::none);
        mWsClient.set_access_channels(
            ::websocketpp::log::alevel::connect
            | ::websocketpp::log::alevel::disconnect
            | ::websocketpp::log::alevel::http
            | ::websocketpp::log::alevel::fail
        );
        mWsClient.set_error_channels(::websocketpp::log::elevel::none);
        mWsClient.set_error_channels(
            ::websocketpp::log::elevel::info
            | ::websocketpp::log::elevel::warn
            | ::websocketpp::log::elevel::rerror
            | ::websocketpp::log::elevel::fatal
        );
    }

    void init () {
        // Called immediately post-construction. AcceptorImpl now has access to shared_from_this().
        auto self = this->shared_from_this();
        mWsClient.set_open_handler(std::bind(&ConnectorImpl::handleOpen, self, _1));
        mWsClient.set_fail_handler(std::bind(&ConnectorImpl::handleFail, self, _1));
    }

    ~ConnectorImpl () {
        boost::system::error_code ec;
        close(ec);
    }

    void close (boost::system::error_code& ec) {
        auto self = this->shared_from_this();
        ec = {};
        mContext.post([self, this, ec]() mutable {
            for (auto&& conPair : mNascentConnections) {
                // Don't want first->close() to accidentally call the handler, which we will go
                // ahead and do ourselves.
                conPair.first->set_open_handler(nullptr);
                conPair.first->set_fail_handler(nullptr);
                conPair.first->close(
                    ::websocketpp::close::status::normal, "Closing nascent connection", ec);
                if (ec) {
                    BOOST_LOG(mLog) << "Error closing WebSocket connection: " << ec.message();
                }
                conPair.second.handler(boost::asio::error::operation_aborted);
            }
            mNascentConnections.clear();
        });
    }

    template <class CompletionToken>
    BOOST_ASIO_INITFN_RESULT_TYPE(CompletionToken, void(boost::system::error_code))
    asyncConnect (MessageQueue& mq,
        const std::string& host, const std::string& service,
        CompletionToken&& token)
    {
        util::asio::AsyncCompletion<
            CompletionToken, void(boost::system::error_code)
        > init { std::forward<CompletionToken>(token) };

        auto uri = std::make_shared<::websocketpp::uri>(false, host, service, "");
        auto& handler = init.handler;
        auto self = this->shared_from_this();
        mContext.post([&mq, uri, handler, self, this]() mutable {
            auto ec = boost::system::error_code{};
            auto con = mWsClient.get_connection(uri, ec);
            if (ec) {
                handler(ec);
                return;
            }
            bool success;
            std::tie(std::ignore, success) = mNascentConnections.insert(
                std::make_pair(con, NascentConnectionData{handler, std::ref(mq)}));
            if (success) {
                mWsClient.connect(con);
            }
            else {
                assert(false);
                handler(boost::asio::error::operation_aborted); // FIXME come up with a real error?
            }
        });

        return init.result.get();
    }

private:
    void handleFail (::websocketpp::connection_hdl hdl) {
        auto ec = boost::system::error_code{};
        auto con = mWsClient.get_con_from_hdl(hdl, ec);
        if (!ec) {
            ec = con->get_transport_ec();
            // If no transport error was recorded, make one up -- we're in the fail handler.
            handleOpenOrFail(con, ec ? ec : make_error_code(boost::asio::error::network_down));
        }
        else {
            BOOST_LOG(mLog) << "Fail handler could not get connection pointer: " << ec.message();
        }
    }

    void handleOpen (::websocketpp::connection_hdl hdl) {
        auto ec = boost::system::error_code{};
        auto con = mWsClient.get_con_from_hdl(hdl, ec);
        if (!ec) {
            handleOpenOrFail(con, ec);
        }
        else {
            BOOST_LOG(mLog) << "Open handler could not get connection pointer: " << ec.message();
        }
    }

    void handleOpenOrFail (ConnectionPtr con, const boost::system::error_code& ec) {
        assert(con);
        auto iter = mNascentConnections.find(con);
        if (iter != mNascentConnections.end()) {
            auto& data = iter->second;
            auto handler = data.handler;
            auto& mq = data.mq.get();
            mNascentConnections.erase(iter);
            // The newly opened connection has handlers which contain shared_ptrs to this.
            // Destroy them as soon as possible.
            mContext.post([con] {
                con->set_open_handler(nullptr);
                con->set_fail_handler(nullptr);
            });
            mq.setConnectionPtr(con);
            handler(ec);
        }
        else {
            BOOST_LOG(mLog) << "Open handler could not find nascent connection pointer";
        }
    }

    boost::asio::io_service& mContext;
    ::websocketpp::client<Config> mWsClient;

    struct NascentConnectionData {
        std::function<void(boost::system::error_code)> handler;
        std::reference_wrapper<MessageQueue> mq;
    };
    std::map<ConnectionPtr, NascentConnectionData> mNascentConnections;

    mutable util::log::Logger mLog;
};

class Connector : public util::asio::TransparentIoObject<ConnectorImpl> {
public:
    explicit Connector (boost::asio::io_service& context)
        : util::asio::TransparentIoObject<ConnectorImpl>(context)
    {
        this->get_implementation()->init();
    }

    using MessageQueue = ConnectorImpl::MessageQueue;
    UTIL_ASIO_DECL_ASYNC_METHOD(asyncConnect)
};

}}} // namespace util::asio::ws

#endif
