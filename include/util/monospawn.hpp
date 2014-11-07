#ifndef UTIL_MONOSPAWN_HPP
#define UTIL_MONOSPAWN_HPP

#include "util/tmp_file_lock.hpp"
#include "util/toposixtime.hpp"

#include <boost/interprocess/sync/scoped_lock.hpp>

#include <exception>
#include <fstream>
#include <utility>

namespace util {

/* A monospawn object provides a way to ensure that only a single instance of a
 * program is running at a time. It is intended to be instantiated near the top
 * of a program's main function. It is implemented using a file lock, so the
 * lock will be released in the event a process either crashes or exits. */
class Monospawn {
public:
    struct DuplicateProcess : public std::exception {
        const char* what () const noexcept {
            return "An instance of this program is already running.";
        }
    };

    /* Acquire a monospawn lock, using name to identify the current program.
     * Wait no longer than timeout to acquire the lock.
     *
     * Throws DuplicateProcess if timeout elapses while waiting to get the
     * lock. This signifies that a process that identifies itself as an
     * instance of this program is currently running.
     *
     * Throws FileLockError if there is a problem setting up the file lock.
     * This is generally unrecoverable, and means external intervention is
     * required (i.e., delete the lock file, change permissions, investigate
     * ACLs) to fix the situation. */
    template <typename Rep, typename Period>
    Monospawn (const char* name, std::chrono::duration<Rep, Period> timeout)
            : mMutex(name) {
        using std::swap;

        auto stopTime = boost::posix_time::microsec_clock::universal_time() +
            toPosixTime(timeout);
        boost::interprocess::scoped_lock<tmp_file_lock> lock { mMutex, stopTime };
        swap(mLock, lock);

        if (!mLock.owns()) {
            throw DuplicateProcess();
        }
    }

private:
    tmp_file_lock mMutex;
    boost::interprocess::scoped_lock<tmp_file_lock> mLock;
};

} // namespace util

#endif
