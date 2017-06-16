// Copyright (c) 2017 Barobo, Inc.
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

// rpc_stream, a stream wrapper which holds a reference to an underlying Asio, Beast, or SFP
// stream/message queue. It provides access to the underlying stream or message queue, and adds one
// asynchronous function, async_write, which writes a message to the next layer in such a way that
// concurrent writes (from the same thread, at least) are safe.

// In retrospect, this is not nearly as useful as I was hoping, since SFP streams are already
// phased, since their reads may interfere with their writes. Ugggh.

#ifndef COMPOSED_RPC_STREAM_HPP
#define COMPOSED_RPC_STREAM_HPP

#include <composed/phaser.hpp>
#include <composed/op.hpp>

#include <pb_asio.hpp>

#include <beast/core/handler_alloc.hpp>
#include <beast/core/async_result.hpp>
#include <beast/core/flat_buffer.hpp>
#include <beast/websocket/stream.hpp>

#include <boost/asio.hpp>

#include <boost/asio/yield.hpp>

namespace composed {

// =======================================================================================

template <class AsyncStream, class TxMessageType, class RxMessageType>
class rpc_stream {
    // nanopb::encodes the argument to async_write().

public:
    template <class... Args>
    explicit rpc_stream(Args&&... args)
        : next_layer_(std::forward<Args>(args)...)
        , strand(next_layer_.get_io_service())
        , read_phaser(strand)
    {}

private:
    template <class LoopBody, class Handler = void(boost::system::error_code)>
    struct read_loop_op;

    template <class Handler>
    struct read_loop_handler;

    template <class Handler>
    auto make_read_loop_handler(Handler&& handler);

    template <class Handler = void(boost::system::error_code)>
    struct stop_read_loop_op;

    template <class Handler = void(boost::system::error_code)>
    struct write_op;

public:
    boost::asio::io_service& get_io_service() { return next_layer_.get_io_service(); }
    auto& next_layer() { return next_layer_; }
    const auto& next_layer() const { return next_layer_; }
    auto& lowest_layer() { return next_layer_.lowest_layer(); }
    const auto& lowest_layer() const { return next_layer_.lowest_layer(); }

    template <class LoopBody, class Token>
    auto async_run_read_loop(LoopBody&& loop_body, Token&& token) {
        beast::async_completion<Token, void(boost::system::error_code)> init{token};

        BOOST_ASSERT(!read_loop_running);
        read_loop_running = true;

        composed::operation<read_loop_op<std::decay_t<LoopBody>>>{}(*this,
                std::forward<LoopBody>(loop_body),
                make_read_loop_handler(std::move(init.completion_handler)));

        return init.result.get();
    }

    template <class Token>
    auto async_stop_read_loop(Token&& token) {
        return composed::operation<stop_read_loop_op<>>{}(*this, std::forward<Token>(token));
    }

    template <class T, class Token>
    auto async_write(const T& message, Token&& token) {
        auto tx_msg = TxMessageType{};
        nanopb::assign(tx_msg.arg, message);
        return composed::operation<write_op<>>{}(*this, tx_msg, std::forward<Token>(token));
    }

private:
    AsyncStream next_layer_;
    boost::asio::io_service::strand strand;
    composed::phaser<boost::asio::io_service::strand&> read_phaser;
    RxMessageType message;
    bool read_loop_running = false;
};

template <class AsyncStream, class TxMessageType, class RxMessageType>
template <class Handler>
struct rpc_stream<AsyncStream, TxMessageType, RxMessageType>::write_op: boost::asio::coroutine {
    using handler_type = Handler;
    using allocator_type = beast::handler_alloc<char, handler_type>;

    rpc_stream& self;

    beast::basic_flat_buffer<allocator_type> buffer;
    // We use a flat buffer because SFP streams require a `const_buffer` argument to `async_write`.
    // When the SFP implementations loosens this requirement to a `ConstBufferSequence`, it'd be
    // good to go back to a multi buffer.

    composed::associated_logger_t<handler_type> lg;
    boost::system::error_code ec;

    template <class T>
    write_op(handler_type& h, rpc_stream& s, const T& message)
        : self(s)
        , buffer(allocator_type(h))
        , lg(composed::get_associated_logger(h))
    {
        auto success = nanopb::encode(buffer, message);
        BOOST_ASSERT(success);
    }

    void operator()(composed::op<write_op>& op);
};

template <class AsyncStream, class TxMessageType, class RxMessageType>
template <class Handler>
void rpc_stream<AsyncStream, TxMessageType, RxMessageType>::write_op<Handler>::operator()(composed::op<write_op>& op) {
    if (!ec) reenter(this) {
        yield return self.next_layer_.async_write(buffer.data(), op(ec));
    }
    op.complete(ec);
}

template <class AsyncStream, class TxMessageType, class RxMessageType>
template <class Handler>
struct rpc_stream<AsyncStream, TxMessageType, RxMessageType>::read_loop_handler {
    rpc_stream& self;
    composed::work_guard<composed::phaser<boost::asio::io_service::strand&>> work;
    Handler handler;

    void operator()(const boost::system::error_code& ec) {
        self.read_loop_running = false;
        handler(ec);
    }

    using logger_type = associated_logger_t<Handler>;
    logger_type get_logger() const {
        return get_associated_logger(handler);
    }

    friend void* asio_handler_allocate(size_t size, read_loop_handler* self) {
        using boost::asio::asio_handler_allocate;
        return asio_handler_allocate(size, &self->handler);
    }

    friend void asio_handler_deallocate(void* pointer, size_t size, read_loop_handler* self) {
        using boost::asio::asio_handler_deallocate;
        asio_handler_deallocate(pointer, size, &self->handler);
    }

    template <class Function>
    friend void asio_handler_invoke(Function&& f, read_loop_handler* self) {
        using boost::asio::asio_handler_invoke;
        asio_handler_invoke(std::forward<Function>(f), &self->handler);
    }

    friend bool asio_handler_is_continuation(read_loop_handler* self) {
        return true;
    }
};

template <class AsyncStream, class TxMessageType, class RxMessageType>
template <class Handler>
auto rpc_stream<AsyncStream, TxMessageType, RxMessageType>::make_read_loop_handler(Handler&& handler) {
    return read_loop_handler<std::decay_t<Handler>>{*this, composed::make_work_guard(read_phaser),
                        std::forward<Handler>(handler)};
};

template <class AsyncStream, class TxMessageType, class RxMessageType>
template <class LoopBody, class Handler>
struct rpc_stream<AsyncStream, TxMessageType, RxMessageType>::read_loop_op: boost::asio::coroutine {
    using handler_type = Handler;
    using allocator_type = beast::handler_alloc<char, handler_type>;

    rpc_stream& self;
    beast::basic_multi_buffer<allocator_type> buf;

    LoopBody loop_body;

    composed::associated_logger_t<handler_type> lg;
    boost::system::error_code ec;

    template <class DeducedLoopBody>
    read_loop_op(handler_type& h, rpc_stream& s, DeducedLoopBody&& f)
        : self(s)
        , buf(256, allocator_type(h))
        , loop_body(std::forward<DeducedLoopBody>(f))
        , lg(composed::get_associated_logger(h))
    {}

    void operator()(composed::op<read_loop_op>& op);
};

template <class AsyncStream, class TxMessageType, class RxMessageType>
template <class LoopBody, class Handler>
void rpc_stream<AsyncStream, TxMessageType, RxMessageType>::read_loop_op<LoopBody, Handler>::
operator()(composed::op<read_loop_op>& op) {
    if (!ec) reenter(this) {
        while (self.read_loop_running) {
            yield return self.next_layer_.async_read(buf, op(ec));

            while (buf.size()) {
                self.message = {};
                if (nanopb::decode(buf, self.message)) {
                    yield {
                        auto h = op();
                        auto visitor = [this, h](const auto& x) {
                            loop_body(x, std::move(h));
                        };
                        if (!nanopb::visit(visitor, self.message.arg)) {
                            BOOST_LOG(lg) << "unrecognized message";
                            self.next_layer_.get_io_service().post(std::move(h));
                        }
                        return;
                    }
                }
                else {
                    BOOST_LOG(lg) << "decoding error";
                }
            }
        }
    }
    op.complete(ec);
}

template <class AsyncStream, class TxMessageType, class RxMessageType>
template <class Handler>
struct rpc_stream<AsyncStream, TxMessageType, RxMessageType>::stop_read_loop_op: boost::asio::coroutine {
    using handler_type = Handler;
    using allocator_type = beast::handler_alloc<char, handler_type>;

    rpc_stream& self;

    composed::associated_logger_t<handler_type> lg;
    boost::system::error_code ec;

    stop_read_loop_op(handler_type& h, rpc_stream& s)
        : self(s)
        , lg(composed::get_associated_logger(h))
    {}

    void operator()(composed::op<stop_read_loop_op>& op);
};

template <class AsyncStream, class TxMessageType, class RxMessageType>
template <class Handler>
void rpc_stream<AsyncStream, TxMessageType, RxMessageType>::stop_read_loop_op<Handler>::
operator()(composed::op<stop_read_loop_op>& op) {
    reenter(this) {
        if (self.read_loop_running) {
            self.read_loop_running = false;
            self.next_layer_.cancel(ec);
        }
        yield return self.read_phaser.dispatch(op());
        yield return self.read_phaser.get_io_service().post(op());
    }
    op.complete(ec);
}

// =======================================================================================

class websocket {
    // Adapts a beast::websocket::stream to match the interface of sfp::stream. I.e., it:
    //   - serializes concurrent calls to async_write(), i.e., it puts each call in a separate phase

public:
    using stream_type = beast::websocket::stream<boost::asio::ip::tcp::socket&>;

    explicit websocket(stream_type& stream)
        : next_layer_(stream)
        , strand(next_layer_.get_io_service())
        , write_phaser(strand)
    {}

private:
    template <class Handler = void(boost::system::error_code)>
    struct write_op;

public:
    boost::asio::io_service& get_io_service() { return next_layer_.get_io_service(); }
    auto& next_layer() { return next_layer_; }
    const auto& next_layer() const { return next_layer_; }
    auto& lowest_layer() { return next_layer_.lowest_layer(); }
    const auto& lowest_layer() const { return next_layer_.lowest_layer(); }

    void cancel(boost::system::error_code& ec) {
        next_layer_.next_layer().cancel(ec);
    }

    template <class DynamicBuffer, class Token>
    auto async_read(DynamicBuffer& buffer, Token&& token) {
        return next_layer_.async_read(buffer, std::forward<Token>(token));
    }

    template <class Token>
    auto async_write(const boost::asio::const_buffer& buffer, Token&& token) {
        return composed::operation<write_op<>>{}(*this, buffer, std::forward<Token>(token));
    }

private:
    stream_type& next_layer_;
    boost::asio::io_service::strand strand;
    composed::phaser<boost::asio::io_service::strand&> write_phaser;
};

template <class Handler>
struct websocket::write_op: boost::asio::coroutine {
    using handler_type = Handler;
    using allocator_type = beast::handler_alloc<char, handler_type>;

    websocket& self;
    boost::asio::const_buffers_1 buffer;

    composed::work_guard<composed::phaser<boost::asio::io_service::strand&>> work;

    composed::associated_logger_t<handler_type> lg;
    boost::system::error_code ec;

    write_op(handler_type& h, websocket& s, const boost::asio::const_buffer& b)
        : self(s)
        , buffer(b)
        , lg(composed::get_associated_logger(h))
    {}

    void operator()(composed::op<write_op>& op);
};

template <class Handler>
void websocket::write_op<Handler>::operator()(composed::op<write_op>& op) {
    if (!ec) reenter(this) {
        yield return self.write_phaser.dispatch(op());
        work = composed::make_work_guard(self.write_phaser);
        yield return self.next_layer_.async_write(buffer, op(ec));
    }
    op.complete(ec);
}

}  // composed

#include <boost/asio/unyield.hpp>

#endif
