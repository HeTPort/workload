#include "vulkan_shader_loader.h"

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#ifndef GPU_AVS_VULKAN_SHADER_DIR
#define GPU_AVS_VULKAN_SHADER_DIR "."
#endif

namespace gpu_avs {

static bool IsAbsolutePath(const std::string& path) {
    if (path.empty()) {
        return false;
    }

#if defined(_WIN32)
    if (path.size() >= 2 && path[1] == ':') {
        return true;
    }
#endif

    return path[0] == '/';
}

static std::string JoinPath(const std::string& dir, const std::string& file) {
    if (dir.empty()) {
        return file;
    }

    char last = dir.back();
    if (last == '/' || last == '\\') {
        return dir + file;
    }

    return dir + "/" + file;
}

static std::vector<std::string> BuildCandidateShaderDirs(
    const WorkloadConfig& cfg
) {
    std::vector<std::string> dirs;

    if (!cfg.shader_dir.empty()) {
        dirs.push_back(cfg.shader_dir);
    }

    const char* env_dir = std::getenv("GPU_AVS_SHADER_DIR");
    if (env_dir && env_dir[0]) {
        dirs.emplace_back(env_dir);
    }

    dirs.emplace_back(GPU_AVS_VULKAN_SHADER_DIR);
    dirs.emplace_back("./shaders/vulkan");
    dirs.emplace_back("/data/local/tmp/shaders/vulkan");

    return dirs;
}

static bool ReadSpvFile(
    const std::string& path,
    std::vector<uint32_t>& spv,
    std::string& error
) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) {
        return false;
    }

    std::streamsize size = in.tellg();

    if (size <= 0 || (size % 4) != 0) {
        error = "invalid SPIR-V size: " + path;
        return false;
    }

    in.seekg(0, std::ios::beg);

    spv.resize(static_cast<size_t>(size) / sizeof(uint32_t));

    if (!in.read(reinterpret_cast<char*>(spv.data()), size)) {
        error = "failed to read SPIR-V file: " + path;
        return false;
    }

    return true;
}

bool LoadVulkanSpv(
    const WorkloadConfig& cfg,
    const std::string& filename,
    std::vector<uint32_t>& spv,
    std::string& error
) {
    spv.clear();
    std::vector<std::string> tried_paths;

    if (IsAbsolutePath(filename)) {
        tried_paths.push_back(filename);

        if (ReadSpvFile(filename, spv, error)) {
            return true;
        }

        if (!error.empty()) {
            return false;
        }
    } else {
        auto dirs = BuildCandidateShaderDirs(cfg);

        for (const auto& dir : dirs) {
            std::string path = JoinPath(dir, filename);
            tried_paths.push_back(path);

            if (ReadSpvFile(path, spv, error)) {
                return true;
            }

            if (!error.empty()) {
                return false;
            }
        }
    }

    std::ostringstream os;
    os << "failed to find Vulkan SPIR-V file: "
       << filename
       << ", tried=[";

    for (size_t i = 0; i < tried_paths.size(); ++i) {
        if (i != 0) {
            os << ", ";
        }
        os << tried_paths[i];
    }

    os << "]";

    error = os.str();
    return false;
}

} // namespace gpu_avs
