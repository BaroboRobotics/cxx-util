#include <util/blob.hpp>
#include <util/intelhex.hpp>

#include <algorithm>
#include <list>

namespace util {

Blob makeBlobFromIntelHex (const char* begin, const char* end) {
    namespace ihex = util::intelhex;

    auto records = std::list<Blob>{};
    auto grammar = ihex::Grammar<decltype(begin)>{};
    if (!ihex::qi::parse(begin, end, grammar >> ihex::qi::eoi, records)) {
        throw BlobError{"Intel HEX parse error"};
    }

    if (records.empty()) { return {}; } // Paranoia

    // The records should already be in ascending order. Trust, but verify. ;)
    records.sort([](const Blob& lhs, const Blob& rhs) {
        return lhs.address() < rhs.address();
    });

    auto sizeEstimate = records.front().code().size() * records.size();
    auto code = std::vector<uint8_t>{};
    code.reserve(sizeEstimate);

    // Concatenate the blobs parsed from the HEX file
    const auto startAddress = records.front().address();
    auto cursor = startAddress;
    for (auto&& record : records) {
        if (record.address() != cursor) {
            throw BlobError{"Noncontiguous addresses"};
        }
        code.insert(code.end(), record.code().begin(), record.code().end());
        cursor += record.code().size();
    }

    return {startAddress, std::move(code)};
}

} // stk