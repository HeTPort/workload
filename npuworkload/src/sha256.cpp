#include "npu_avs/sha256.h"

#include <array>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace npu_avs {
namespace {

constexpr std::array<uint32_t, 64> kConstants = {
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
    0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
    0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
    0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
    0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
    0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
    0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
    0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U, 0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U
};

uint32_t RotateRight(uint32_t value, unsigned count) {
    return (value >> count) | (value << (32U - count));
}

class Sha256 {
public:
    void Update(const uint8_t* data, size_t size) {
        for (size_t i = 0; i < size; ++i) {
            block_[block_size_++] = data[i];
            if (block_size_ == block_.size()) {
                Transform();
                bit_count_ += 512U;
                block_size_ = 0;
            }
        }
    }

    std::array<uint8_t, 32> Final() {
        const uint64_t total_bits = bit_count_ + static_cast<uint64_t>(block_size_) * 8U;
        block_[block_size_++] = 0x80U;
        if (block_size_ > 56U) {
            while (block_size_ < 64U) block_[block_size_++] = 0U;
            Transform();
            block_size_ = 0;
        }
        while (block_size_ < 56U) block_[block_size_++] = 0U;
        for (int shift = 56; shift >= 0; shift -= 8) {
            block_[block_size_++] = static_cast<uint8_t>((total_bits >> shift) & 0xffU);
        }
        Transform();

        std::array<uint8_t, 32> digest{};
        for (size_t i = 0; i < state_.size(); ++i) {
            digest[i * 4U] = static_cast<uint8_t>(state_[i] >> 24U);
            digest[i * 4U + 1U] = static_cast<uint8_t>(state_[i] >> 16U);
            digest[i * 4U + 2U] = static_cast<uint8_t>(state_[i] >> 8U);
            digest[i * 4U + 3U] = static_cast<uint8_t>(state_[i]);
        }
        return digest;
    }

private:
    void Transform() {
        uint32_t words[64]{};
        for (size_t i = 0; i < 16U; ++i) {
            words[i] = (static_cast<uint32_t>(block_[i * 4U]) << 24U) |
                       (static_cast<uint32_t>(block_[i * 4U + 1U]) << 16U) |
                       (static_cast<uint32_t>(block_[i * 4U + 2U]) << 8U) |
                       static_cast<uint32_t>(block_[i * 4U + 3U]);
        }
        for (size_t i = 16U; i < 64U; ++i) {
            const uint32_t s0 = RotateRight(words[i - 15U], 7U) ^ RotateRight(words[i - 15U], 18U) ^
                                (words[i - 15U] >> 3U);
            const uint32_t s1 = RotateRight(words[i - 2U], 17U) ^ RotateRight(words[i - 2U], 19U) ^
                                (words[i - 2U] >> 10U);
            words[i] = words[i - 16U] + s0 + words[i - 7U] + s1;
        }

        uint32_t a = state_[0];
        uint32_t b = state_[1];
        uint32_t c = state_[2];
        uint32_t d = state_[3];
        uint32_t e = state_[4];
        uint32_t f = state_[5];
        uint32_t g = state_[6];
        uint32_t h = state_[7];
        for (size_t i = 0; i < 64U; ++i) {
            const uint32_t sum1 = RotateRight(e, 6U) ^ RotateRight(e, 11U) ^ RotateRight(e, 25U);
            const uint32_t choice = (e & f) ^ (~e & g);
            const uint32_t temp1 = h + sum1 + choice + kConstants[i] + words[i];
            const uint32_t sum0 = RotateRight(a, 2U) ^ RotateRight(a, 13U) ^ RotateRight(a, 22U);
            const uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
            const uint32_t temp2 = sum0 + majority;
            h = g;
            g = f;
            f = e;
            e = d + temp1;
            d = c;
            c = b;
            b = a;
            a = temp1 + temp2;
        }
        state_[0] += a;
        state_[1] += b;
        state_[2] += c;
        state_[3] += d;
        state_[4] += e;
        state_[5] += f;
        state_[6] += g;
        state_[7] += h;
    }

    std::array<uint32_t, 8> state_ = {
        0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
        0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U
    };
    std::array<uint8_t, 64> block_{};
    size_t block_size_ = 0;
    uint64_t bit_count_ = 0;
};

std::string DigestHex(const std::array<uint8_t, 32>& digest) {
    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (uint8_t byte : digest) output << std::setw(2) << static_cast<unsigned>(byte);
    return output.str();
}

} // namespace

std::string Sha256Hex(const uint8_t* data, size_t size) {
    Sha256 sha;
    sha.Update(data, size);
    return DigestHex(sha.Final());
}

std::string Sha256Hex(const std::string& data) {
    return Sha256Hex(reinterpret_cast<const uint8_t*>(data.data()), data.size());
}

bool Sha256File(const std::string& path, std::string& digest, std::string& error) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        error = "failed to open file for SHA-256: " + path;
        return false;
    }
    Sha256 sha;
    std::array<uint8_t, 64U * 1024U> buffer{};
    while (input) {
        input.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize count = input.gcount();
        if (count > 0) sha.Update(buffer.data(), static_cast<size_t>(count));
    }
    if (!input.eof()) {
        error = "failed while reading file for SHA-256: " + path;
        return false;
    }
    digest = DigestHex(sha.Final());
    return true;
}

} // namespace npu_avs
