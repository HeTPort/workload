#pragma once

#include "cpu_avs/backend.h"

#include <algorithm>
#include <cstdint>
#include <exception>
#include <limits>
#include <new>
#include <string>
#include <thread>
#include <vector>

namespace cpu_avs {

inline uint64_t RotateLeft64(uint64_t value, unsigned shift) {
    shift &= 63U;
    return shift == 0U ? value : (value << shift) | (value >> (64U - shift));
}

inline uint64_t Mix64(uint64_t value) {
    value ^= value >> 30U;
    value *= 0xbf58476d1ce4e5b9ULL;
    value ^= value >> 27U;
    value *= 0x94d049bb133111ebULL;
    return value ^ (value >> 31U);
}

inline uint64_t SaturatingMultiply(uint64_t left, uint64_t right) {
    if (left != 0U && right > std::numeric_limits<uint64_t>::max() / left) {
        return std::numeric_limits<uint64_t>::max();
    }
    return left * right;
}

template <typename Kernel>
BackendStatus RunWorkers(const WorkloadConfig& cfg, uint64_t operations_per_worker,
                         Kernel kernel, CpuBatchResult& out, std::string& error) {
    try {
        std::vector<uint64_t> checksums(cfg.threads, 0U);
        std::vector<std::exception_ptr> exceptions(cfg.threads);

        const auto invoke = [&](uint32_t worker) {
            try {
                checksums[worker] = kernel(worker);
            } catch (...) {
                exceptions[worker] = std::current_exception();
            }
        };

        if (cfg.threads == 1U) {
            invoke(0U);
        } else {
            std::vector<std::thread> workers;
            workers.reserve(cfg.threads);
            for (uint32_t worker = 0; worker < cfg.threads; ++worker) {
                workers.emplace_back(invoke, worker);
            }
            for (std::thread& worker : workers) {
                worker.join();
            }
        }

        uint64_t combined = 0x6a09e667f3bcc909ULL;
        for (uint32_t worker = 0; worker < cfg.threads; ++worker) {
            if (exceptions[worker]) {
                try {
                    std::rethrow_exception(exceptions[worker]);
                } catch (const std::exception& exception) {
                    error = std::string("worker exception: ") + exception.what();
                } catch (...) {
                    error = "worker exception: unknown error";
                }
                return BackendStatus::Error;
            }
            combined = Mix64(combined ^ checksums[worker] ^ static_cast<uint64_t>(worker));
        }

        out.operation_count = SaturatingMultiply(operations_per_worker, cfg.threads);
        out.checksum = combined;
        return BackendStatus::Ok;
    } catch (const std::bad_alloc&) {
        error = "CPU backend allocation failed";
        return BackendStatus::AllocationFail;
    } catch (const std::exception& exception) {
        error = std::string("CPU backend error: ") + exception.what();
        return BackendStatus::Error;
    } catch (...) {
        error = "CPU backend unknown error";
        return BackendStatus::UnknownError;
    }
}

} // namespace cpu_avs
