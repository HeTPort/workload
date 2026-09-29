#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace npu_avs {

std::string Sha256Hex(const uint8_t* data, size_t size);
std::string Sha256Hex(const std::string& data);
bool Sha256File(const std::string& path, std::string& digest, std::string& error);

} // namespace npu_avs
