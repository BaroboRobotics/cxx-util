// Copyright (c) 2014-2016 Barobo, Inc.
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#ifndef UTIL_ASIO_WS_MESSAGEQUEUE_HPP
#define UTIL_ASIO_WS_MESSAGEQUEUE_HPP

#include <util/log.hpp>

#include <util/producerconsumerqueue.hpp>
#include <util/asio/asynccompletion.hpp>
#include <util/asio/transparentservice.hpp>

#include <websocketpp/connection.hpp>

#include <boost/asio/io_service.hpp>

namespace util { namespace asio { namespace ws {

using namespace std::placeholders;

typedef void ReceiveHandlerSignature(boost::system::error_code, size_t);
typedef void SendHandlerSignature(boost::system::error_code);

template <class Config>
class MessageQueueImpl : public std::enable_shared_from_this<MessageQueueImpl<Config>> {
public:
    using Connection = websocketpp::connection<Config>;
    using ConnectionPtr = typename Connection::ptr;
    using MessagePtr = typename Connection::message_ptr;

    MessageQueueImpl (boost::asio::io_service& ios)
        : mContext(ios)
    {
        mLog.add_attribute("Protocol", boost::log::attributes::constant<std::string>("WSQ"));
    }

    ~MessageQueueImpl () {
        if (mPtr) {
            // mPtr's handlers have shared_ptrs back to this, so we need to break the cycle. This
            // shouldn't be done in close() because close() may be called from one of the handlers
            // we're nullifying here.
            mPtr->set_message_handler(nullptr);
            mPtr->set_close_handler(nullptr);
        }
        boost::system::error_code ec;
        close(ec);
    }

    void close (boost::system::error_code& ec) {
        while (mReceiveQueue.depth() < 0) {
            mReceiveQueue.produce(boost::asio::error::operation_aborted, nullptr);
        }
        while (mReceiveQueue.depth() > 0) {
            mReceiveQueue.consume([this](boost::system::error_code ec2, MessagePtr msg) {
                if (!ec2) {
                    BOOST_LOG(mLog) << "Discarding " << msg->get_payload().size()
                        << " byte message";
                }
                else {
                    BOOST_LOG(mLog) << "Discarding error message: " << ec2.message();
                }
            });
        }
        ec = {};
        if (mPtr) {
            mPtr->close(websocketpp::close::status::normal, "", ec);
        }
    }

    std::string getRemoteEndpoint () const {
        return mPtr->get_remote_endpoint();
    }

    template <class CompletionToken>
    BOOST_ASIO_INITFN_RESULT_TYPE(CompletionToken, SendHandlerSignature)
    asyncSend (boost::asio::const_buffer buffer, CompletionToken&& token) {
        util::asio::AsyncCompletion<
            CompletionToken, SendHandlerSignature
        > init { std::forward<CompletionToken>(token) };

        assert(mPtr);
        auto ec = mPtr->send(boost::asio::buffer_cast<void const*>(buffer),
                boost::asio::buffer_size(buffer));
        // Don't know if it's possible to actually detect when the send has completed.
        mContext.post(std::bind(init.handler, ec));

        return init.result.get();
    }

    template <class CompletionToken>
    BOOST_ASIO_INITFN_RESULT_TYPE(CompletionToken, ReceiveHandlerSignature)
    asyncReceive (boost::asio::mutable_buffer buffer, CompletionToken&& token) {
        util::asio::AsyncCompletion<
            CompletionToken, ReceiveHandlerSignature
        > init { std::forward<CompletionToken>(token) };

        auto& handler = init.handler;
        auto self = this->shared_from_this();
        mContext.post([buffer, handler, self, this]() mutable {
            auto ec = mPtr->get_transport_ec();
            if (!ec) {
                auto consume = [buffer, handler, self, this]
                        (boost::system::error_code ec2, MessagePtr msg) mutable {
                    size_t nCopied = 0;
                    if (!ec2) {
                        nCopied = boost::asio::buffer_copy(buffer,
                                boost::asio::buffer(msg->get_payload()));
                        ec2 = nCopied == msg->get_payload().size()
                            ? boost::system::error_code()
                            : make_error_code(boost::asio::error::message_size);
                    }
                    mContext.post(std::bind(handler, ec2, nCopied));
                };
                mReceiveQueue.consume(consume);
            }
            else {
                mContext.post(std::bind(handler, ec, 0));
            }
        });

        return init.result.get();
    }

    void setConnectionPtr (ConnectionPtr ptr) {
        mPtr = ptr;
        auto self = this->shared_from_this();
        mPtr->set_message_handler(std::bind(&MessageQueueImpl::handleMessage, self, _1, _2));
        mPtr->set_close_handler(std::bind(&MessageQueueImpl::handleClose, self, _1));
    }

private:
    void handleMessage (websocketpp::connection_hdl, MessagePtr msg) {
        //BOOST_LOG(mLog) << "Received " << msg->get_payload().size();
        mReceiveQueue.produce(boost::system::error_code(), msg);
    }

    void handleClose (websocketpp::connection_hdl) {
        BOOST_LOG(mLog) << "WebSocket message queue closing: "
            << mPtr->get_remote_close_code() << " ["
            << mPtr->get_remote_close_reason() << "]";
        auto ec = mPtr->get_transport_ec();
        ec = ec ? ec : boost::asio::error::operation_aborted;
        mReceiveQueue.produce(ec, nullptr);
    }

    boost::asio::io_service& mContext;
    ConnectionPtr mPtr;
    util::ProducerConsumerQueue<boost::system::error_code, MessagePtr> mReceiveQueue;

    mutable util::log::Logger mLog;
};

template <class Config>
class MessageQueue : public util::asio::TransparentIoObject<MessageQueueImpl<Config>> {
public:
    explicit MessageQueue (boost::asio::io_service& ios)
        : util::asio::TransparentIoObject<MessageQueueImpl<Config>>(ios)
    {}

    using ConnectionPtr = typename MessageQueueImpl<Config>::ConnectionPtr;
    void setConnectionPtr (ConnectionPtr ptr) {
        this->get_implementation()->setConnectionPtr(ptr);
    }

    std::string getRemoteEndpoint () const {
        return this->get_implementation()->getRemoteEndpoint();
    }

    UTIL_ASIO_DECL_ASYNC_METHOD(asyncSend)
    UTIL_ASIO_DECL_ASYNC_METHOD(asyncReceive)
};

}}} // namespace util::asio::ws

#endif
