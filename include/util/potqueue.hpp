#ifndef POTQUEUE_HPP
#define POTQUEUE_HPP

#include "potringbuffer.hpp"

namespace util {

template <class T, size_t N>
class PotQueue {
public:
    PotQueue() {}
    virtual volatile T& at (size_t index) volatile { return mRingbuffer.at(index); }
    size_t size() volatile const { return mRingbuffer.size(); }
    bool empty () const { return mRingbuffer.empty(); }
    bool empty () volatile const { return mRingbuffer.empty(); }
    bool full () const { return mRingbuffer.full(); }
    bool full () volatile const { return mRingbuffer.full(); }
    T& front () { return mRingbuffer.front(); }
    volatile T& front () volatile { return mRingbuffer.front(); }
    void push (const T& elem) { mRingbuffer.pushBack(elem); }
    void push (const T& elem) volatile { mRingbuffer.pushBack(elem); }
    void pop () { mRingbuffer.popFront(); }
    void pop () volatile { mRingbuffer.popFront(); }

private:
    PotRingbuffer<T, N> mRingbuffer;
};

} // namespace util

#endif
