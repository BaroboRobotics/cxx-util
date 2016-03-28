#ifndef UTIL_WINDOWS_MSI_HPP
#define UTIL_WINDOWS_MSI_HPP

#include <util/windows/error.hpp>

#include <string>

#include <msi.h>
#include <windows.h>

namespace util { namespace windows { namespace msi {

class Session {
public:
    enum class Button {
        OK, CANCEL
    };

    explicit Session (MSIHANDLE handle) : mHandle(handle) {}

    // Retrieve an MSI property.
    std::string getProperty (const std::string& key) const;

    // Set an MSI property. To remove a property, pass an empty string as value.
    void setProperty (const std::string& key, const std::string& value);

    void log (const std::string& text);
    void messageBoxOk (const std::string& text);
    Button messageBoxOkCancel (const std::string& text);

private:
    MSIHANDLE mHandle;
};

}}} // util::windows::msi

#endif
