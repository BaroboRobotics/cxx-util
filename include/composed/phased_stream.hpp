// Copyright (c) 2017 Barobo, Inc.
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

// phased_stream, a stream wrapper which holds a reference to an underlying Asio, Beast, or SFP
// stream/message queue. It provides access to the underlying stream or message queue, and adds one
// asynchronous function, async_write, which writes a message to the next layer in such a way that
// concurrent writes (from the same thread, at least) are safe.

// In retrospect, this is not nearly as useful as I was hoping, since SFP streams are already
// phased, since their reads may interfere with their writes. Ugggh.

#ifndef COMPOSED_PHASED_STREAM_HPP
#define COMPOSED_PHASED_STREAM_HPP

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
    // Serializes concurrent calls to async_write(), i.e., it puts each call in a separate phase.

public:
    template <class... Args>
    phased_stream(typename Executor::handler_type& h, Args&&... args)
        : stream(std::forward<Args>(args)...)
        , write_phaser(stream.get_io_service(), h)
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
    AsyncStream stream;
    composed::phaser<Executor> write_phaser;
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

template <class Executor, class AsyncStream>
template <class Handler>
void phased_stream<Executor, AsyncStream>::
write_event_op<Handler>::
operator()(composed::op<write_event_op>& op) {
    if (!ec) reenter(this) {
        yield return self.write_phaser.dispatch(op());
        work = composed::make_work_guard(self.write_phaser);
        yield return self.stream.async_write(buf.data(), op(ec));
    }
    else {
        BOOST_LOG(lg) << "write_event_op error: " << ec.message();
    }
    op.complete(ec);
}

}  // composed

#include <boost/asio/unyield.hpp>

#endif