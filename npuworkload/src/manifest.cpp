#include "npu_avs/manifest.h"

#include "npu_avs/json.h"
#include "npu_avs/sha256.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

namespace npu_avs {
namespace {

namespace fs = std::filesystem;

bool CheckKeys(const JsonValue& value, const std::set<std::string>& allowed,
               const std::set<std::string>& required, const std::string& location,
               std::string& error) {
    if (value.GetType() != JsonValue::Type::Object) {
        error = location + " must be an object";
        return false;
    }
    const auto& object = value.AsObject();
    for (const auto& entry : object) {
        if (allowed.count(entry.first) == 0U) {
            error = location + " contains unknown field '" + entry.first + "'";
            return false;
        }
    }
    for (const std::string& key : required) {
        if (object.count(key) == 0U) {
            error = location + " is missing required field '" + key + "'";
            return false;
        }
    }
    return true;
}

const JsonValue* Member(const JsonValue& object, const std::string& key) {
    const auto iterator = object.AsObject().find(key);
    return iterator == object.AsObject().end() ? nullptr : &iterator->second;
}

bool ReadString(const JsonValue& object, const std::string& key, std::string& output,
                const std::string& location, std::string& error) {
    const JsonValue* value = Member(object, key);
    if (!value || value->GetType() != JsonValue::Type::String) {
        error = location + "." + key + " must be a string";
        return false;
    }
    output = value->AsText();
    return true;
}

bool ParseUnsigned(const JsonValue& value, uint64_t maximum, uint64_t& output,
                   const std::string& location, std::string& error) {
    if (value.GetType() != JsonValue::Type::Number) {
        error = location + " must be an unsigned integer";
        return false;
    }
    const std::string& text = value.AsText();
    if (text.empty() || text.front() == '-' ||
        !std::all_of(text.begin(), text.end(), [](unsigned char c) { return std::isdigit(c) != 0; })) {
        error = location + " must be an unsigned integer";
        return false;
    }
    try {
        size_t consumed = 0;
        const unsigned long long parsed = std::stoull(text, &consumed, 10);
        if (consumed != text.size() || parsed > maximum) throw std::out_of_range("range");
        output = static_cast<uint64_t>(parsed);
        return true;
    } catch (...) {
        error = location + " is outside its permitted integer range";
        return false;
    }
}

bool ParseSigned(const JsonValue& value, int32_t& output, const std::string& location,
                 std::string& error) {
    if (value.GetType() != JsonValue::Type::Number) {
        error = location + " must be an integer";
        return false;
    }
    const std::string& text = value.AsText();
    size_t start = (!text.empty() && text.front() == '-') ? 1U : 0U;
    if (start == text.size() ||
        !std::all_of(text.begin() + static_cast<std::ptrdiff_t>(start), text.end(),
            [](unsigned char c) { return std::isdigit(c) != 0; })) {
        error = location + " must be an integer";
        return false;
    }
    try {
        size_t consumed = 0;
        const long long parsed = std::stoll(text, &consumed, 10);
        if (consumed != text.size() || parsed < std::numeric_limits<int32_t>::min() ||
            parsed > std::numeric_limits<int32_t>::max()) throw std::out_of_range("range");
        output = static_cast<int32_t>(parsed);
        return true;
    } catch (...) {
        error = location + " is outside int32 range";
        return false;
    }
}

bool ParseDouble(const JsonValue& value, double& output, const std::string& location,
                 std::string& error) {
    if (value.GetType() != JsonValue::Type::Number) {
        error = location + " must be a number";
        return false;
    }
    try {
        size_t consumed = 0;
        output = std::stod(value.AsText(), &consumed);
        if (consumed != value.AsText().size() || !std::isfinite(output)) throw std::out_of_range("number");
        return true;
    } catch (...) {
        error = location + " must be a finite number";
        return false;
    }
}

bool ParseDataType(const std::string& text, TensorDataType& type) {
    if (text == "int8") type = TensorDataType::Int8;
    else if (text == "uint8") type = TensorDataType::UInt8;
    else if (text == "int16") type = TensorDataType::Int16;
    else if (text == "int32") type = TensorDataType::Int32;
    else if (text == "float16") type = TensorDataType::Float16;
    else if (text == "float32") type = TensorDataType::Float32;
    else return false;
    return true;
}

bool ParseLayout(const std::string& text, TensorLayout& layout) {
    if (text == "scalar") layout = TensorLayout::Scalar;
    else if (text == "nc") layout = TensorLayout::NC;
    else if (text == "nchw") layout = TensorLayout::NCHW;
    else if (text == "nhwc") layout = TensorLayout::NHWC;
    else if (text == "ntc") layout = TensorLayout::NTC;
    else if (text == "raw") layout = TensorLayout::Raw;
    else return false;
    return true;
}

bool ParseQuantization(const JsonValue& value, QuantizationSpec& quantization,
                       const std::string& location, std::string& error) {
    if (!CheckKeys(value, {"mode", "scales", "zero_points", "axis"}, {"mode"}, location, error)) {
        return false;
    }
    std::string mode;
    if (!ReadString(value, "mode", mode, location, error)) return false;
    if (mode == "none") quantization.mode = QuantizationMode::None;
    else if (mode == "per_tensor") quantization.mode = QuantizationMode::PerTensor;
    else if (mode == "per_axis") quantization.mode = QuantizationMode::PerAxis;
    else {
        error = location + ".mode is unsupported";
        return false;
    }
    if (const JsonValue* scales = Member(value, "scales")) {
        if (scales->GetType() != JsonValue::Type::Array) {
            error = location + ".scales must be an array";
            return false;
        }
        for (size_t i = 0; i < scales->AsArray().size(); ++i) {
            double parsed = 0.0;
            if (!ParseDouble(scales->AsArray()[i], parsed,
                             location + ".scales[" + std::to_string(i) + "]", error)) return false;
            if (parsed > std::numeric_limits<float>::max()) {
                error = location + ".scales contains a value outside float32 range";
                return false;
            }
            quantization.scales.push_back(static_cast<float>(parsed));
        }
    }
    if (const JsonValue* zero_points = Member(value, "zero_points")) {
        if (zero_points->GetType() != JsonValue::Type::Array) {
            error = location + ".zero_points must be an array";
            return false;
        }
        for (size_t i = 0; i < zero_points->AsArray().size(); ++i) {
            int32_t parsed = 0;
            if (!ParseSigned(zero_points->AsArray()[i], parsed,
                             location + ".zero_points[" + std::to_string(i) + "]", error)) return false;
            quantization.zero_points.push_back(parsed);
        }
    }
    if (const JsonValue* axis = Member(value, "axis")) {
        if (!ParseSigned(*axis, quantization.axis, location + ".axis", error)) return false;
    }
    return true;
}

bool ParseTensorSpec(const JsonValue& value, bool asset, TensorSpec& spec,
                     std::string& file, std::string& sha256,
                     const std::string& location, std::string& error) {
    std::set<std::string> allowed = {"name", "dtype", "shape", "layout", "quantization"};
    std::set<std::string> required = allowed;
    if (asset) {
        allowed.insert("file");
        allowed.insert("sha256");
        required.insert("file");
        required.insert("sha256");
    }
    if (!CheckKeys(value, allowed, required, location, error)) return false;
    std::string dtype;
    std::string layout;
    if (!ReadString(value, "name", spec.name, location, error) ||
        !ReadString(value, "dtype", dtype, location, error) ||
        !ReadString(value, "layout", layout, location, error)) return false;
    if (!ParseDataType(dtype, spec.data_type)) {
        error = location + ".dtype is unsupported";
        return false;
    }
    if (!ParseLayout(layout, spec.layout)) {
        error = location + ".layout is unsupported";
        return false;
    }
    const JsonValue* shape = Member(value, "shape");
    if (!shape || shape->GetType() != JsonValue::Type::Array) {
        error = location + ".shape must be an array";
        return false;
    }
    for (size_t i = 0; i < shape->AsArray().size(); ++i) {
        uint64_t dimension = 0;
        if (!ParseUnsigned(shape->AsArray()[i], std::numeric_limits<uint32_t>::max(), dimension,
                           location + ".shape[" + std::to_string(i) + "]", error)) return false;
        spec.shape.push_back(static_cast<uint32_t>(dimension));
    }
    if (!ParseQuantization(*Member(value, "quantization"), spec.quantization,
                           location + ".quantization", error)) return false;
    if (!ValidateTensorSpec(spec, error)) {
        error = location + ": " + error;
        return false;
    }
    if (asset) {
        if (!ReadString(value, "file", file, location, error) ||
            !ReadString(value, "sha256", sha256, location, error)) return false;
    }
    return true;
}

bool IsSha256(const std::string& value) {
    return value.size() == 64U && std::all_of(value.begin(), value.end(), [](unsigned char c) {
        return std::isxdigit(c) != 0;
    });
}

std::string Lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

bool IsWithin(const fs::path& root, const fs::path& candidate) {
    auto root_it = root.begin();
    auto candidate_it = candidate.begin();
    for (; root_it != root.end(); ++root_it, ++candidate_it) {
        if (candidate_it == candidate.end() || *root_it != *candidate_it) return false;
    }
    return true;
}

bool ResolveAsset(const fs::path& root, const std::string& relative, fs::path& resolved,
                  std::string& error) {
    const fs::path requested = fs::u8path(relative);
    if (relative.empty() || requested.is_absolute() || requested.has_root_name()) {
        error = "asset path must be non-empty and relative: " + relative;
        return false;
    }
    std::error_code code;
    resolved = fs::weakly_canonical(root / requested, code);
    if (code) {
        error = "failed to resolve asset path '" + relative + "': " + code.message();
        return false;
    }
    if (!IsWithin(root, resolved)) {
        error = "asset path escapes the declared asset root: " + relative;
        return false;
    }
    return true;
}

bool ReadFile(const fs::path& path, uint64_t expected_bytes, std::vector<uint8_t>& data,
              std::string& error) {
    std::error_code code;
    const uintmax_t size = fs::file_size(path, code);
    if (code) {
        error = "failed to inspect tensor asset '" + path.string() + "': " + code.message();
        return false;
    }
    if (size != expected_bytes || size > std::numeric_limits<size_t>::max()) {
        error = "tensor asset byte size does not match its descriptor: " + path.string();
        return false;
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        error = "failed to open tensor asset: " + path.string();
        return false;
    }
    data.resize(static_cast<size_t>(size));
    if (!data.empty()) input.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
    if (!input || input.gcount() != static_cast<std::streamsize>(data.size())) {
        error = "failed to read complete tensor asset: " + path.string();
        return false;
    }
    return true;
}

bool LoadTensorAssets(const JsonValue& array, const fs::path& root,
                      std::vector<TensorSpec>& specs, TensorSet& tensors,
                      const std::string& location, std::string& error) {
    if (array.GetType() != JsonValue::Type::Array || array.AsArray().empty()) {
        error = location + " must be a non-empty array";
        return false;
    }
    std::unordered_set<std::string> names;
    for (size_t i = 0; i < array.AsArray().size(); ++i) {
        TensorBuffer tensor;
        std::string file;
        std::string expected_hash;
        const std::string item_location = location + "[" + std::to_string(i) + "]";
        if (!ParseTensorSpec(array.AsArray()[i], true, tensor.spec, file, expected_hash,
                             item_location, error)) return false;
        if (!names.insert(tensor.spec.name).second) {
            error = location + " contains duplicate tensor name '" + tensor.spec.name + "'";
            return false;
        }
        if (!IsSha256(expected_hash)) {
            error = item_location + ".sha256 must contain 64 hexadecimal characters";
            return false;
        }
        fs::path resolved;
        if (!ResolveAsset(root, file, resolved, error)) return false;
        uint64_t bytes = 0;
        if (!TensorByteSize(tensor.spec, bytes, error) || !ReadFile(resolved, bytes, tensor.data, error)) return false;
        std::string actual_hash;
        if (!Sha256File(resolved.string(), actual_hash, error)) return false;
        if (Lower(expected_hash) != actual_hash) {
            error = "SHA-256 mismatch for tensor asset: " + resolved.string();
            return false;
        }
        specs.push_back(tensor.spec);
        tensors.push_back(std::move(tensor));
    }
    return true;
}

bool LoadOutputSpecs(const JsonValue& array, std::vector<TensorSpec>& specs, std::string& error) {
    if (array.GetType() != JsonValue::Type::Array || array.AsArray().empty()) {
        error = "outputs must be a non-empty array";
        return false;
    }
    std::unordered_set<std::string> names;
    for (size_t i = 0; i < array.AsArray().size(); ++i) {
        TensorSpec spec;
        std::string ignored_file;
        std::string ignored_hash;
        if (!ParseTensorSpec(array.AsArray()[i], false, spec, ignored_file, ignored_hash,
                             "outputs[" + std::to_string(i) + "]", error)) return false;
        if (!names.insert(spec.name).second) {
            error = "outputs contains duplicate tensor name '" + spec.name + "'";
            return false;
        }
        specs.push_back(std::move(spec));
    }
    return true;
}

void AppendU32(std::string& data, uint32_t value) {
    for (unsigned shift = 0; shift < 32U; shift += 8U) data.push_back(static_cast<char>((value >> shift) & 0xffU));
}

void AppendString(std::string& data, const std::string& value) {
    AppendU32(data, static_cast<uint32_t>(value.size()));
    data.append(value);
}

void AppendSpecs(std::string& data, const std::vector<TensorSpec>& specs) {
    AppendU32(data, static_cast<uint32_t>(specs.size()));
    for (const TensorSpec& spec : specs) {
        AppendString(data, spec.name);
        AppendString(data, TensorDataTypeName(spec.data_type));
        AppendString(data, TensorLayoutName(spec.layout));
        AppendString(data, QuantizationModeName(spec.quantization.mode));
        AppendU32(data, static_cast<uint32_t>(spec.shape.size()));
        for (uint32_t dimension : spec.shape) AppendU32(data, dimension);
        AppendU32(data, static_cast<uint32_t>(spec.quantization.scales.size()));
        for (float scale : spec.quantization.scales) {
            uint32_t bits = 0;
            std::memcpy(&bits, &scale, sizeof(bits));
            AppendU32(data, bits);
        }
        for (int32_t zero_point : spec.quantization.zero_points) AppendU32(data, static_cast<uint32_t>(zero_point));
        AppendU32(data, static_cast<uint32_t>(spec.quantization.axis));
    }
}

} // namespace

std::string ComputeTensorSignatureSha256(const ProfileSpec& profile) {
    std::string canonical;
    AppendString(canonical, profile.id);
    AppendString(canonical, profile.version);
    AppendString(canonical, profile.workload);
    AppendSpecs(canonical, profile.inputs);
    AppendSpecs(canonical, profile.outputs);
    return Sha256Hex(canonical);
}

bool LoadProfileManifest(const std::string& path, const std::string& expected_profile,
                         ProfileManifest& manifest, std::string& error) {
    manifest = ProfileManifest{};
    error.clear();
    std::error_code code;
    const fs::path manifest_path = fs::weakly_canonical(fs::u8path(path), code);
    if (code || !fs::is_regular_file(manifest_path)) {
        error = "failed to resolve profile manifest: " + path;
        return false;
    }
    const uintmax_t manifest_size = fs::file_size(manifest_path, code);
    if (code || manifest_size > 1024U * 1024U) {
        error = "profile manifest exceeds the 1 MiB size limit";
        return false;
    }
    std::ifstream input(manifest_path, std::ios::binary);
    std::ostringstream buffer;
    buffer << input.rdbuf();
    if (!input && !input.eof()) {
        error = "failed to read profile manifest: " + manifest_path.string();
        return false;
    }
    const std::string text = buffer.str();
    JsonValue root;
    if (!ParseJson(text, root, error)) return false;
    if (!CheckKeys(root,
            {"schema_version", "profile", "asset_root", "model", "sample_id",
             "preprocessing_version", "postprocessing_version", "inputs", "outputs",
             "goldens", "tolerance"},
            {"schema_version", "profile", "asset_root", "model", "sample_id",
             "preprocessing_version", "postprocessing_version", "inputs", "outputs",
             "goldens", "tolerance"}, "manifest", error)) return false;

    uint64_t schema_version = 0;
    if (!ParseUnsigned(*Member(root, "schema_version"), 1U, schema_version,
                       "schema_version", error) || schema_version != 1U) {
        if (error.empty()) error = "unsupported manifest schema_version";
        return false;
    }
    manifest.schema_version = 1;

    const JsonValue& profile = *Member(root, "profile");
    if (!CheckKeys(profile, {"id", "version", "workload", "default_backend"},
                   {"id", "version", "workload", "default_backend"}, "profile", error)) return false;
    if (!ReadString(profile, "id", manifest.profile.id, "profile", error) ||
        !ReadString(profile, "version", manifest.profile.version, "profile", error) ||
        !ReadString(profile, "workload", manifest.profile.workload, "profile", error) ||
        !ReadString(profile, "default_backend", manifest.profile.default_backend, "profile", error)) return false;
    const ProfileSpec* registered = FindProfileSpec(manifest.profile.id);
    if (!registered || manifest.profile.id != expected_profile ||
        manifest.profile.version != registered->version ||
        manifest.profile.workload != registered->workload ||
        manifest.profile.default_backend != registered->default_backend) {
        error = "manifest profile identity does not match the registered selected profile";
        return false;
    }
    if (!registered->implemented) {
        error = "selected profile has no approved manifest contract";
        return false;
    }
    manifest.profile.implemented = registered->implemented;

    std::string asset_root_text;
    if (!ReadString(root, "asset_root", asset_root_text, "manifest", error)) return false;
    const fs::path declared_root = fs::u8path(asset_root_text);
    if (asset_root_text.empty() || declared_root.is_absolute() || declared_root.has_root_name()) {
        error = "asset_root must be a non-empty relative path";
        return false;
    }
    const fs::path manifest_directory = manifest_path.parent_path();
    const fs::path asset_root = fs::weakly_canonical(manifest_directory / declared_root, code);
    if (code || !fs::is_directory(asset_root) || !IsWithin(manifest_directory, asset_root)) {
        error = "asset_root must resolve to a directory within the manifest directory";
        return false;
    }

    const JsonValue& model = *Member(root, "model");
    if (!CheckKeys(model, {"format", "file", "sha256", "provenance"},
                   {"format", "file", "sha256", "provenance"}, "model", error) ||
        !ReadString(model, "format", manifest.model.format, "model", error) ||
        !ReadString(model, "file", manifest.model.path, "model", error) ||
        !ReadString(model, "sha256", manifest.model.sha256, "model", error) ||
        !ReadString(model, "provenance", manifest.model.provenance, "model", error)) return false;
    if (manifest.model.provenance.empty()) {
        error = "model.provenance must not be empty";
        return false;
    }
    if (manifest.model.format == "none") {
        if (!manifest.model.path.empty() || !manifest.model.sha256.empty()) {
            error = "model format 'none' must use empty file and sha256 values";
            return false;
        }
    } else {
        if (!IsSha256(manifest.model.sha256)) {
            error = "model.sha256 must contain 64 hexadecimal characters";
            return false;
        }
        fs::path model_path;
        if (!ResolveAsset(asset_root, manifest.model.path, model_path, error)) return false;
        std::string actual_hash;
        if (!Sha256File(model_path.string(), actual_hash, error)) return false;
        if (Lower(manifest.model.sha256) != actual_hash) {
            error = "SHA-256 mismatch for model asset: " + model_path.string();
            return false;
        }
        manifest.model.path = model_path.string();
        manifest.model.sha256 = actual_hash;
    }
    manifest.profile.model_format = manifest.model.format;
    manifest.profile.model_path = manifest.model.path;
    manifest.profile.model_sha256 = manifest.model.sha256;

    if (!ReadString(root, "sample_id", manifest.sample_id, "manifest", error) ||
        !ReadString(root, "preprocessing_version", manifest.preprocessing_version, "manifest", error) ||
        !ReadString(root, "postprocessing_version", manifest.postprocessing_version, "manifest", error)) return false;
    if (manifest.sample_id.empty() || manifest.preprocessing_version.empty() ||
        manifest.postprocessing_version.empty()) {
        error = "sample and preprocessing/postprocessing versions must not be empty";
        return false;
    }

    if (!LoadTensorAssets(*Member(root, "inputs"), asset_root, manifest.profile.inputs,
                          manifest.inputs, "inputs", error) ||
        !LoadOutputSpecs(*Member(root, "outputs"), manifest.profile.outputs, error)) return false;
    if (ComputeTensorSignatureSha256(manifest.profile) !=
        ComputeTensorSignatureSha256(*registered)) {
        error = "manifest tensor signature does not match the registered profile contract";
        return false;
    }

    const JsonValue& goldens = *Member(root, "goldens");
    if (goldens.GetType() != JsonValue::Type::Array) {
        error = "goldens must be an array";
        return false;
    }
    if (!goldens.AsArray().empty()) {
        std::vector<TensorSpec> golden_specs;
        if (!LoadTensorAssets(goldens, asset_root, golden_specs, manifest.goldens,
                              "goldens", error)) return false;
        if (!ValidateTensorSet(manifest.goldens, manifest.profile.outputs, error)) {
            error = "goldens: " + error;
            return false;
        }
    }

    const JsonValue& tolerance = *Member(root, "tolerance");
    if (!CheckKeys(tolerance, {"mode", "absolute", "relative"},
                   {"mode", "absolute", "relative"}, "tolerance", error) ||
        !ReadString(tolerance, "mode", manifest.tolerance.mode, "tolerance", error) ||
        !ParseDouble(*Member(tolerance, "absolute"), manifest.tolerance.absolute,
                     "tolerance.absolute", error) ||
        !ParseDouble(*Member(tolerance, "relative"), manifest.tolerance.relative,
                     "tolerance.relative", error)) return false;
    if ((manifest.tolerance.mode != "exact" && manifest.tolerance.mode != "absolute_relative") ||
        manifest.tolerance.absolute < 0.0 || manifest.tolerance.relative < 0.0 ||
        (manifest.tolerance.mode == "exact" &&
         (manifest.tolerance.absolute != 0.0 || manifest.tolerance.relative != 0.0))) {
        error = "tolerance policy is invalid";
        return false;
    }

    manifest.manifest_path = manifest_path.string();
    manifest.asset_root = asset_root.string();
    manifest.manifest_sha256 = Sha256Hex(text);
    manifest.tensor_signature_sha256 = ComputeTensorSignatureSha256(manifest.profile);
    return true;
}

} // namespace npu_avs
