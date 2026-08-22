#include "cpu_avs/crc32.h"

#include <iomanip>
#include <sstream>

namespace cpu_avs {

uint32_t Crc32(const uint8_t* data, size_t size) {
    uint32_t crc = 0xffffffffU;
    for (size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            const uint32_t mask = 0U - (crc & 1U);
            crc = (crc >> 1U) ^ (0xedb88320U & mask);
        }
    }
    return ~crc;
}

std::string Crc32Hex(const uint8_t* data, size_t size) {
    std::ostringstream os;
    os << std::hex << std::setfill('0') << std::setw(8) << Crc32(data, size);
    return os.str();
}

uint64_t SimpleChecksum64(const uint8_t* data, size_t size) {
    uint64_t hash = 1469598103934665603ULL;
    for (size_t i = 0; i < size; ++i) {
        hash ^= data[i];
        hash *= 1099511628211ULL;
    }
    return hash;
}

std::string Hex64(uint64_t value) {
    std::ostringstream os;
    os << std::hex << std::setfill('0') << std::setw(16) << value;
    return os.str();
}

std::string Checksum64Hex(const uint8_t* data, size_t size) {
    return Hex64(SimpleChecksum64(data, size));
}

} // namespace cpu_avs
