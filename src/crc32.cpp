#include "gpu_avs/crc32.h"

#include <iomanip>
#include <sstream>

namespace gpu_avs {

uint32_t Crc32(const uint8_t* data, size_t size) {
    static uint32_t table[256];
    static bool table_initialized = false;

    if (!table_initialized) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;

            for (uint32_t j = 0; j < 8; ++j) {
                if (c & 1U) {
                    c = 0xEDB88320U ^ (c >> 1);
                } else {
                    c >>= 1;
                }
            }

            table[i] = c;
        }

        table_initialized = true;
    }

    uint32_t crc = 0xFFFFFFFFU;

    for (size_t i = 0; i < size; ++i) {
        crc = table[(crc ^ data[i]) & 0xFFU] ^ (crc >> 8);
    }

    return crc ^ 0xFFFFFFFFU;
}

std::string Crc32Hex(const uint8_t* data, size_t size) {
    uint32_t crc = Crc32(data, size);

    std::ostringstream os;
    os << "0x"
       << std::hex
       << std::uppercase
       << std::setw(8)
       << std::setfill('0')
       << crc;

    return os.str();
}

uint64_t SimpleChecksum64(const uint8_t* data, size_t size) {
    uint64_t sum = 0xcbf29ce484222325ULL;

    for (size_t i = 0; i < size; ++i) {
        sum ^= static_cast<uint64_t>(data[i]);
        sum *= 0x100000001b3ULL;
    }

    return sum;
}

std::string Checksum64Hex(const uint8_t* data, size_t size) {
    uint64_t checksum = SimpleChecksum64(data, size);

    std::ostringstream os;
    os << "0x"
       << std::hex
       << std::uppercase
       << std::setw(16)
       << std::setfill('0')
       << checksum;

    return os.str();
}

} // namespace gpu_avs
