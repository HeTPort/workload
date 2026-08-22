#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace cpu_avs {

uint32_t Crc32(const uint8_t* data, size_t size);
std::string Crc32Hex(const uint8_t* data, size_t size);
uint64_t SimpleChecksum64(const uint8_t* data, size_t size);
std::string Checksum64Hex(const uint8_t* data, size_t size);
std::string Hex64(uint64_t value);

} // namespace cpu_avs
