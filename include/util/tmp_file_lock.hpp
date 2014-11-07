#ifndef UTIL_TMP_FILE_LOCK_HPP
#define UTIL_TMP_FILE_LOCK_HPP

#include <boost/interprocess/sync/file_lock.hpp>
#include <boost/filesystem.hpp>

#include <fstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace util {

struct FileLockError : std::exception {
public:
    explicit FileLockError (std::string msg) : mMsg(msg) { }

    const char* what () {
        return mMsg.c_str();
    }

private:
    std::string mMsg;
};

/* A wrapper around Boost.Interprocess file_lock which performs the following:
 *   - prepend the system temporary directory onto the file name
 *   - create the file to be locked in case it does not exist
 *   - translate all exceptions from the locking and unlocking functions to
 *     a FileLockError exception
 *
 * tmp_file_lock is meant to be used with Boost.Interprocess's RAII locking
 * classes, such as shared_lock and sharable_lock. For this reason its API is
 * written in the Boost convention instead of camelcasing.
 *
 * TODO
 * - remove lock files when no longer needed
 * - This entire class is a good spot to write some OS-specific code instead of
 *   wrapping Boost's file_lock. We'll need to do so in order to remove lock
 *   files when they are no longer needed while avoiding race conditions [1].
 *   Additionally, we need to worry about access control: files should be
 *   chmodded to 0600 if possible.
 *
 * [1] http://stackoverflow.com/questions/17708885/flock-removing-locked-file-without-race-condition
 */
class tmp_file_lock {
public:
    /* Build an absolute path to a file named name in the system temporary
     * directory (/tmp, C:\Temp, etc.). Touch the file to make sure it exists,
     * then instantiate a Boost.Interprocess file_lock on the file.
     *
     * Throws FileLockError if no system temporary directory exists, or if there is
     * a file creation or permissions error. */
    tmp_file_lock (std::string name) {
        /* Build the absolute path of the lock file. */
        boost::filesystem::path filepath;
        try {
            filepath = boost::filesystem::temp_directory_path();
        }
        catch (boost::filesystem::filesystem_error& exc) {
            throw FileLockError("Unable to get system temporary directory");
        }

        filepath /= name;
        mFilename = filepath.string();

        /* Touch the file. */
        if (!std::ofstream(mFilename.c_str()).flush()) {
            throw FileLockError("Unable to open " + mFilename + " for writing");
        }

        /* Instantiate the Boost.Interprocess file_lock. */
        try {
            using std::swap;

            boost::interprocess::file_lock flock { mFilename.c_str() };
            swap(mFlock, flock);
        }
        catch (boost::interprocess::interprocess_exception& exc) {
            throw FileLockError("Unable to instantiate file lock (mutex) on " +
                    mFilename);
        }
    }

    void lock () {
        try {
            mFlock.lock();
        }
        catch (boost::interprocess::interprocess_exception& exc) {
            throw FileLockError("Error locking " + mFilename);
        }
    }

    bool try_lock () {
        try {
            return mFlock.try_lock();
        }
        catch (boost::interprocess::interprocess_exception& exc) {
            throw FileLockError("Error trying to lock " + mFilename);
        }
    }

    bool timed_lock (const boost::posix_time::ptime& abs_time) {
        try {
            return mFlock.timed_lock(abs_time);
        }
        catch (boost::interprocess::interprocess_exception& exc) {
            throw FileLockError("Error executing timed lock on " + mFilename);
        }
    }

    void unlock () {
        try {
            mFlock.unlock();
        }
        catch (boost::interprocess::interprocess_exception& exc) {
            throw FileLockError("Error unlocking " + mFilename);
        }
    }

    void lock_sharable () {
        try {
            mFlock.lock_sharable();
        }
        catch (boost::interprocess::interprocess_exception& exc) {
            throw FileLockError("Error sharably locking " + mFilename);
        }
    }

    bool try_lock_sharable () {
        try {
            return mFlock.try_lock_sharable();
        }
        catch (boost::interprocess::interprocess_exception& exc) {
            throw FileLockError("Error trying to sharably lock " + mFilename);
        }
    }

    bool timed_lock_sharable (const boost::posix_time::ptime& abs_time) {
        try {
            return mFlock.timed_lock_sharable(abs_time);
        }
        catch (boost::interprocess::interprocess_exception& exc) {
            throw FileLockError("Error executing timed sharable lock on " + mFilename);
        }
    }

    void unlock_sharable () {
        try {
            mFlock.unlock_sharable();
        }
        catch (boost::interprocess::interprocess_exception& exc) {
            throw FileLockError("Error sharably unlocking " + mFilename);
        }
    }

private:
    std::string mFilename;
    boost::interprocess::file_lock mFlock;
};

} // namespace util

#endif
