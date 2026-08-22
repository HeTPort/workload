#pragma once

#include <cstdint>
#include <string>

namespace cpu_avs {

uint64_t NowMs();
double NowSeconds();
void SleepMs(uint32_t ms);
bool ParseBool(const std::string& value, bool& out);
std::string ToLower(std::string value);

} // namespace cpu_avs
