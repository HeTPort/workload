#pragma once

#include <cstdint>
#include <string>

namespace gpu_avs {

uint64_t NowMs();
double NowSeconds();

void SleepMs(uint32_t ms);

bool ParseBool(const std::string& s, bool& out);

std::string ToLower(std::string s);

} // namespace gpu_avs
