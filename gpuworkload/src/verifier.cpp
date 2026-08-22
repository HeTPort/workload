#include "gpu_avs/verifier.h"

#include "gpu_avs/crc32.h"

#include <cmath>
#include <fstream>
#include <sstream>

namespace gpu_avs {

Verifier::Verifier(const WorkloadConfig& cfg)
    : cfg_(cfg) {}

bool Verifier::Enabled() const {
    return cfg_.verify_mode != "none" || cfg_.generate_golden;
}

std::string Verifier::ComputeChecksum(const ReadbackBuffer& buffer) const {
    if (buffer.data.empty()) {
        return "";
    }

    if (cfg_.verify_mode == "checksum") {
        return Checksum64Hex(buffer.data.data(), buffer.data.size());
    }

    return Crc32Hex(buffer.data.data(), buffer.data.size());
}

VerifyResult Verifier::Verify(
    const ReadbackBuffer& buffer,
    uint64_t frame_index
) {
    if (cfg_.verify_mode == "none") {
        VerifyResult r;
        r.pass = true;
        r.frame = frame_index;
        r.verify_mode = cfg_.verify_mode;
        r.message = "verification disabled";
        return r;
    }

    if (cfg_.verify_mode == "crc" || cfg_.verify_mode == "checksum") {
        return VerifyChecksumLike(buffer, frame_index);
    }

    if (cfg_.verify_mode == "golden-image") {
        return VerifyGoldenFileExact(buffer, frame_index, false);
    }

    if (cfg_.verify_mode == "pixel-diff") {
        return VerifyPixelDiff(buffer, frame_index);
    }

    if (cfg_.verify_mode == "compute-compare") {
        return VerifyGoldenFileExact(buffer, frame_index, true);
    }

    VerifyResult r;
    r.pass = false;
    r.frame = frame_index;
    r.verify_mode = cfg_.verify_mode;
    r.message = "unsupported verify mode: " + cfg_.verify_mode;
    return r;
}

VerifyResult Verifier::VerifyChecksumLike(
    const ReadbackBuffer& buffer,
    uint64_t frame_index
) const {
    VerifyResult r;
    r.frame = frame_index;
    r.verify_mode = cfg_.verify_mode;
    r.checksum = ComputeChecksum(buffer);
    r.golden_checksum = cfg_.golden_checksum;

    if (r.checksum.empty()) {
        r.pass = false;
        r.message = "empty readback buffer";
        return r;
    }

    if (cfg_.golden_checksum.empty()) {
        r.pass = true;
        r.message = "golden checksum not provided, checksum recorded only";
        return r;
    }

    if (r.checksum == cfg_.golden_checksum) {
        r.pass = true;
        r.message = "checksum matched";
    } else {
        r.pass = false;
        r.mismatch_count = 1;
        r.message = "checksum mismatch";
    }

    return r;
}

bool Verifier::LoadGoldenFile(
    std::vector<uint8_t>& out,
    std::string& error
) const {
    if (cfg_.golden_file.empty()) {
        error = "golden file is not specified";
        return false;
    }

    std::ifstream in(cfg_.golden_file, std::ios::binary);
    if (!in) {
        error = "failed to open golden file: " + cfg_.golden_file;
        return false;
    }

    in.seekg(0, std::ios::end);
    std::streamoff size = in.tellg();
    in.seekg(0, std::ios::beg);

    if (size < 0) {
        error = "failed to get golden file size";
        return false;
    }

    out.resize(static_cast<size_t>(size));

    if (!out.empty()) {
        in.read(
            reinterpret_cast<char*>(out.data()),
            static_cast<std::streamsize>(out.size())
        );
    }

    return true;
}

VerifyResult Verifier::VerifyGoldenFileExact(
    const ReadbackBuffer& buffer,
    uint64_t frame_index,
    bool is_compute
) const {
    VerifyResult r;
    r.frame = frame_index;
    r.verify_mode = cfg_.verify_mode;
    r.checksum = ComputeChecksum(buffer);
    r.golden_checksum = cfg_.golden_checksum;

    std::vector<uint8_t> golden;
    std::string error;

    if (!LoadGoldenFile(golden, error)) {
        r.pass = false;
        r.message = error;
        return r;
    }

    if (golden.size() != buffer.data.size()) {
        r.pass = false;
        r.mismatch_count = 1;

        std::ostringstream os;
        os << "golden size mismatch, golden="
           << golden.size()
           << ", actual="
           << buffer.data.size();

        r.message = os.str();
        return r;
    }

    uint64_t mismatch = 0;

    for (size_t i = 0; i < golden.size(); ++i) {
        if (golden[i] != buffer.data[i]) {
            mismatch++;
        }
    }

    r.mismatch_count = mismatch;

    if (is_compute) {
        r.compute_mismatch_count = mismatch;
    }

    r.pass = mismatch == 0;
    r.message = r.pass ? "golden file matched" : "golden file mismatch";

    return r;
}

VerifyResult Verifier::VerifyPixelDiff(
    const ReadbackBuffer& buffer,
    uint64_t frame_index
) const {
    VerifyResult r;
    r.frame = frame_index;
    r.verify_mode = cfg_.verify_mode;
    r.checksum = ComputeChecksum(buffer);
    r.golden_checksum = cfg_.golden_checksum;

    std::vector<uint8_t> golden;
    std::string error;

    if (!LoadGoldenFile(golden, error)) {
        r.pass = false;
        r.message = error;
        return r;
    }

    if (golden.size() != buffer.data.size()) {
        r.pass = false;
        r.mismatch_count = 1;

        std::ostringstream os;
        os << "golden size mismatch, golden="
           << golden.size()
           << ", actual="
           << buffer.data.size();

        r.message = os.str();
        return r;
    }

    uint64_t diff_count = 0;
    const double threshold = cfg_.pixel_threshold;

    for (size_t i = 0; i < golden.size(); ++i) {
        double diff = std::abs(
            static_cast<int>(golden[i]) -
            static_cast<int>(buffer.data[i])
        );

        if (diff > threshold) {
            diff_count++;
        }
    }

    r.pixel_diff_count = diff_count;
    r.mismatch_count = diff_count;

    if (diff_count <= cfg_.pixel_max_diff_count) {
        r.pass = true;
        r.message = "pixel diff within threshold";
    } else {
        r.pass = false;
        r.message = "pixel diff exceeds threshold";
    }

    return r;
}

bool Verifier::WriteGoldenFile(
    const ReadbackBuffer& buffer,
    const std::string& path,
    std::string& error
) const {
    if (path.empty()) {
        error = "golden file path is empty";
        return false;
    }

    std::ofstream out(path, std::ios::binary);
    if (!out) {
        error = "failed to open golden output file: " + path;
        return false;
    }

    if (!buffer.data.empty()) {
        out.write(
            reinterpret_cast<const char*>(buffer.data.data()),
            static_cast<std::streamsize>(buffer.data.size())
        );
    }

    return true;
}

} // namespace gpu_avs
