#pragma once

#include <string>

namespace avs {

constexpr unsigned kContractVersion = 2;
constexpr const char* kVersion = "2.1.0";

// Each input layer may contain only one spelling of the verify interval.
// Config and CLI are separate layers, so a CLI alias can override a config key.
inline bool CheckVerifyIntervalKey(const std::string& raw_key, bool& seen,
                                  std::string& error) {
    std::string key = raw_key.rfind("--", 0) == 0 ? raw_key.substr(2) : raw_key;
    for (char& c : key) if (c == '_') c = '-';
    if (key != "verify-interval" && key != "checksum-interval") return true;
    if (seen) {
        error = "duplicate verify interval: use only one verify_interval/checksum_interval spelling per input";
        return false;
    }
    seen = true;
    return true;
}

} // namespace avs
