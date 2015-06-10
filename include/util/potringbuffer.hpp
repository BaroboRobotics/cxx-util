#ifndef POTRINGBUFFER_HPP
#define POTRINGBUFFER_HPP

#include <assert.h>
#include <stdlib.h>

namespace util {

/* Power-of-two sized ringbuffer. */
template <class T, size_t N>
class PotRingbuffer {
    static_assert(N, "PotRingbuffer capacity must be greater than zero");
    static_assert(!(N % 2), "PotRingbuffer capacity must be a power of two");

public:
    PotRingbuffer() {}
    /* Number of elements in ringbuffer. */
    size_t size () volatile const {
        return full() ? N : (mEnd - mBegin) & (N - 1);
    }
    
    /* True if ringbuffer is empty. */
    bool empty () const {
        return mBegin == mEnd;
    }

    /* True if ringbuffer is empty. */
    bool empty () volatile const {
        return mBegin == mEnd;
    }

    /* True if ringbuffer is full. */
    bool full () const {
        return (mBegin ^ N) == mEnd;
    }

    /* True if ringbuffer is full. */
    bool full () volatile const {
        return (mBegin ^ N) == mEnd;
    }

    /* Array-like access, counting forward from begin. */
    T& at (size_t index) {
        return wrappedAccess(mBegin + index);
    }

    /* Array-like access, counting forward from begin. */
    volatile T& at (size_t index) volatile {
        return wrappedAccess(mBegin + index);
    }

    /* Array-like access, counting backward from end. */
    T& reverseAt (size_t index) {
        return wrappedAccess(mEnd - index);
    }

    /* Array-like access, counting backward from end. */
    volatile T& reverseAt (size_t index) volatile {
        return wrappedAccess(mEnd - index);
    }

    /* Access the first element. */
    T& front () {
        return at(0);
    }

    /* Access the first element. */
    volatile T& front () volatile {
        return at(0);
    }

    /* Access the last element. */
    T& back () {
        return reverseAt(1);
    }

    /* Access the last element. */
    volatile T& back () volatile {
        return reverseAt(1);
    }

    /* Append an element to the back. */
    void pushBack (const T& elem) {
        if (full()) {
            incr(mBegin);
        }
        incr(mEnd);
        back() = elem;
    }

    /* Append an element to the back. */
    void pushBack (const T& elem) volatile {
        if (full()) {
            incr(mBegin);
        }
        incr(mEnd);
        back() = elem;
    }

    /* Prepend an element to the front. */
    void pushFront (const T& elem) volatile {
        if (full()) {
            decr(mEnd);
        }
        decr(mBegin);
        front() = elem;
    }

    /* Remove the first element. */
    void popFront () volatile {
        assert(!empty());
        incr(mBegin);
    }

    /* Remove the last element. */
    void popBack () {
        assert(!empty());
        decr(mEnd);
    }

    /* Remove the last element. */
    void popBack () volatile {
        assert(!empty());
        decr(mEnd);
    }

private:
    T& wrappedAccess (size_t index) {
        return mData[index & (N - 1)];
    }

    volatile T& wrappedAccess (volatile size_t index) volatile {
        return mData[index & (N - 1)];
    }

    void add (volatile size_t& beginOrEnd, size_t amount) volatile {
        beginOrEnd = (beginOrEnd + amount) & (2 * N - 1);
    }

    void incr (volatile size_t& beginOrEnd) volatile {
        add(beginOrEnd, 1);
    }

    void decr (volatile size_t& beginOrEnd) volatile {
        add(beginOrEnd, -1);
    }

    size_t mBegin = 0;
    size_t mEnd = 0;
    T mData[N];
};

} // namespace util

#endif
