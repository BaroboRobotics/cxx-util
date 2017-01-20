// Copyright (c) 2014-2016 Barobo, Inc.
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#ifndef UTIL_PRODUCERCONSUMERQUEUE_HPP
#define UTIL_PRODUCERCONSUMERQUEUE_HPP

#include <util/applytuple.hpp>

#include <functional>
#include <queue>
#include <tuple>
#include <utility>

namespace util {

template <class... Data>
class ProducerConsumerQueue {
public:
    template <class H>
    void consume (H&& handler) {
        // Enqueue a pulling function object to be invoked when data are produced. If consume is
        // called before any data are in the buffer, the function object is saved for later
        // invocation. If consume is called when data are in the buffer, the function object is
        // immediately invoked.
        mHandlers.emplace(std::forward<H>(handler));
        post();
    }

    template <class... Ds>
    void produce (Ds&&... data) {
        // Enqueue data to be called as arguments to a pulling function object. If produce is called
        // before any consuming function objects are in the buffer, the data are saved for later
        // invocation on a future function object. If produce is called when a consuming function
        // object is waiting, that function object is immediately invoked.
        mData.emplace(std::make_tuple(std::forward<Ds>(data)...));
        post();
    }

    int depth () const {
        // Depth is:
        //    0   if the queue is empty
        //    > 0 if the queue has supply (surplus data)
        //    < 0 if the queue has demand (surplus consumers)
        return mData.size() - mHandlers.size();
    }

private:
    void post () {
        while (mHandlers.size() && mData.size()) {
            auto handler = mHandlers.front();
            auto result = mData.front();
            mHandlers.pop();
            mData.pop();
            util::applyTuple(handler, result);
        }
    }

    using Handler = std::function<void(Data...)>;
    using DataTuple = std::tuple<Data...>;

    std::queue<Handler> mHandlers;
    std::queue<DataTuple> mData;
};

} // namespace util

#endif
