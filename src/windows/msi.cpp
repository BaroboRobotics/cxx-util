#include <util/windows/error.hpp>
#include <util/windows/msi.hpp>

#include <vector>

#include <msiquery.h>

namespace util { namespace windows { namespace msi {

std::string Session::getProperty (const std::string& key) const {
    DWORD size=0;
    auto err = MsiGetPropertyA(mHandle, key.data(), "", &size);
    if (ERROR_MORE_DATA != err) {
        throw Error{"MsiGetProperty", err};
    }
    // size does not include terminating null
    ++size;
    auto value = std::vector<char>(size);
    err = MsiGetPropertyA(mHandle, key.data(), value.data(), &size);
    if (ERROR_SUCCESS != err) {
        throw Error{"MsiGetProperty", err};
    }
    return value.data();
}

void Session::setProperty (const std::string& key, const std::string& value) {
    auto err = MsiSetPropertyA(mHandle, key.data(), value.data());
    if (ERROR_SUCCESS != err) {
        throw Error{"MsiSetProperty", err};
    }
}

void Session::log (const std::string& line) {
    // PMSIHANDLE calls MsiCloseHandle() on destruction
    auto record = PMSIHANDLE{MsiCreateRecord(1)};
    if (!record) { throw std::runtime_error{"MsiCreateRecord failed"}; }

    auto err = MsiRecordSetStringA(record, 0, "Log: [1]");
    if (err != ERROR_SUCCESS) { throw Error{"MsiRecordSetString", err}; }

    err = MsiRecordSetStringA(record, 1, line.data());
    if (err != ERROR_SUCCESS) { throw Error{"MsiRecordSetString", err}; }

    auto rc = MsiProcessMessage(mHandle, INSTALLMESSAGE_INFO, record);
    if (rc != IDOK) { throw std::runtime_error{"MsiProcessMessage failed"}; }
}

}}} // util::windows::msi