// Copyright (c) 2017 Barobo, Inc.
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

// phased_stream, a stream wrapper which holds a reference to an underlying Asio, Beast, or SFP
// stream/message queue. It provides access to the underlying stream or message queue, and adds one
// asynchronous function, async_write, which writes a message to the next layer in such a way that
// concurrent writes (from the same thread, at least) are safe.

#ifndef COMPOSED_ASYNC_WRITE_HPP
#define COMPOSED_ASYNC_WRITE_HPP

#include <composed/phaser.hpp>
#include <composed/op.hpp>

#include <pb.hpp>

#include <beast/core/handler_alloc.hpp>
#include <beast/core/streambuf.hpp>
#include <beast/websocket/stream.hpp>

#include <boost/asio.hpp>

#include <boost/asio/yield.hpp>

namespace composed {

template <class Executor, class AsyncStream>
struct phased_stream {
    // Guarantees that concurrent calls to async_write() will not interfere with each other.

public:
    phased_stream(AsyncStream& s,
            typename Executor::handler_type& h)
        : write_phaser(s.get_io_service(), h)
        , stream(s)
    {}

private:
    template <class Handler = void(boost::system::error_code)>
    struct write_event_op;

public:
    boost::asio::io_service& get_io_service() { return stream.get_io_service(); }
    AsyncStream& next_layer() { return stream; }

    template <class T, class Token>
    auto async_write(const T& message, Token&& token) {
        return composed::operation<write_event_op<>>{}(*this, message, std::forward<Token>(token));
    }

private:
    composed::phaser<Executor> write_phaser;
    AsyncStream& stream;
};

template <class Executor, class AsyncStream>
template <class Handler>
struct phased_stream<Executor, AsyncStream>::write_event_op: boost::asio::coroutine {
    using handler_type = Handler;
    using allocator_type = beast::handler_alloc<char, handler_type>;

    phased_stream& self;

    composed::work_guard<composed::phaser<Executor>> work;

    beast::basic_streambuf<allocator_type> buf;

    composed::associated_logger_t<handler_type> lg;
    boost::system::error_code ec;

    template <class T>
    write_event_op(handler_type& h, phased_stream& s, const T& message)
        : self(s)
        , buf(256, allocator_type(h))
        , lg(composed::get_associated_logger(h))
    {
        auto success = nanopb::encode(buf, message);
        BOOST_ASSERT(success);
    }

    void operator()(composed::op<write_event_op>& op);
};

namespace _ {

template <class AsyncStream>
struct write_helper;
// Different Asio-compatible stream-like types have different functions to write one whole message.
// This write_helper type contains one static member function, async_write, which calls the
// appropriate function.

template <class AsyncStream>
struct write_helper {
    template <class ConstBufferSequence, class Token>
    static auto async_write(AsyncStream& stream, const ConstBufferSequence& buf, Token&& token) {
        // By default, we can just use boost::asio::async_write.
        return boost::asio::async_write(stream, buf, std::forward<Token>(token));
    }
};

template <class T>
struct write_helper<beast::websocket::stream<T>> {
    template <class ConstBufferSequence, class Token>
    static auto async_write(beast::websocket::stream<T>& stream, 
            const ConstBufferSequence& buf, Token&& token) {
        // Beast websocket streams have their own member function for writing one whole message.
        return stream.async_write(buf, std::forward<Token>(token));
    }
};

}  // _

template <class Executor, class AsyncStream>
template <class Handler>
void phased_stream<Executor, AsyncStream>::
write_event_op<Handler>::
operator()(composed::op<write_event_op>& op) {
    if (!ec) reenter(this) {
        yield return self.write_phaser.dispatch(op());
        work = composed::make_work_guard(self.write_phaser);
        yield return _::write_helper<AsyncStream>::async_write(self.stream, buf.data(), op(ec));
    }
    else {
        BOOST_LOG(lg) << "write_event_op error: " << ec.message();
    }
    op.complete(ec);
}

}  // composed

#include <boost/asio/unyield.hpp>

#endif