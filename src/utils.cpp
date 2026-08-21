#include "gpu_avs/utils.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <thread>

namespace gpu_avs {

uint64_t NowMs() {
    using namespace std::chrono;

    return duration_cast<milliseconds>(
        steady_clock::now().time_since_epoch()
    ).count();
}

double NowSeconds() {
    using namespace std::chrono;

    return duration_cast<duration<double>>(
        steady_clock::now().time_since_epoch()
    ).count();
}

void SleepMs(uint32_t ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

std::string ToLower(std::string s) {
    std::transform(
        s.begin(),
        s.end(),
        s.begin(),
        [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        }
    );

    return s;
}

bool ParseBool(const std::string& s, bool& out) {
    std::string v = ToLower(s);

    if (v == "1" || v == "true" || v == "yes" || v == "on") {
        out = true;
        return true;
    }

    if (v == "0" || v == "false" || v == "no" || v == "off") {
        out = false;
        return true;
    }

    return false;
}

} // namespace gpu_avs
