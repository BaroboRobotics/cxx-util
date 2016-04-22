#ifndef UTIL_PRODUCERCONSUMERQUEUE_HPP
#define UTIL_PRODUCERCONSUMERQUEUE_HPP

#include <util/applytuple.hpp>

#include <functional>
#include <queue>
#include <tuple>
#include <utility>

namespace util {

// A queue supporting three operations, consume, produce, and clear.
//   consume: enqueue a function object to be invoked when data are produced
//   produce: enqueue data to be called as arguments to a pulling function object
//   clear: clear the consuming function object queue by invoking all function objects in excess of
//     the result queue with a set of default parameters; clear the result queue by invoking all
//     data in excess of the function object queue on a default function object
//
// If consume is called before any data are in the buffer, the function object is saved for later
// invocation. If consume is called when data are in the buffer, the function object is
// immediately invoked.
//
// If produce is called before any consuming function objects are in the buffer, the data are saved
// for later invocation on a future function object. If produce is called when a consuming function
// object is waiting, that function object is immediately invoked.
template <class... Data>
class ProducerConsumerQueue {
public:
    template <class H>
    void consume (H&& handler) {
        mHandlers.emplace(std::forward<H>(handler));
        post();
    }

    template <class... Ds>
    void produce (Ds&&... data) {
        mData.emplace(std::make_tuple(std::forward<Ds>(data)...));
        post();
    }

    int depth () const {
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
