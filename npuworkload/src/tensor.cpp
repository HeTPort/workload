#include "npu_avs/tensor.h"

#include <cmath>
#include <limits>
#include <unordered_set>

namespace npu_avs {
namespace {

bool IsValidLayout(TensorLayout layout) {
    switch (layout) {
        case TensorLayout::Scalar:
        case TensorLayout::NC:
        case TensorLayout::NCHW:
        case TensorLayout::NHWC:
        case TensorLayout::NTC:
        case TensorLayout::Raw: return true;
        default: return false;
    }
}

bool QuantizationEqual(const QuantizationSpec& left, const QuantizationSpec& right) {
    return left.mode == right.mode && left.scales == right.scales &&
           left.zero_points == right.zero_points && left.axis == right.axis;
}

bool TensorSpecEqual(const TensorSpec& left, const TensorSpec& right) {
    return left.name == right.name && left.data_type == right.data_type &&
           left.shape == right.shape && left.layout == right.layout &&
           QuantizationEqual(left.quantization, right.quantization);
}

} // namespace

const char* TensorDataTypeName(TensorDataType type) {
    switch (type) {
        case TensorDataType::Int8: return "int8";
        case TensorDataType::UInt8: return "uint8";
        case TensorDataType::Int16: return "int16";
        case TensorDataType::Int32: return "int32";
        case TensorDataType::Float16: return "float16";
        case TensorDataType::Float32: return "float32";
        default: return "unknown";
    }
}

const char* TensorLayoutName(TensorLayout layout) {
    switch (layout) {
        case TensorLayout::Scalar: return "scalar";
        case TensorLayout::NC: return "nc";
        case TensorLayout::NCHW: return "nchw";
        case TensorLayout::NHWC: return "nhwc";
        case TensorLayout::NTC: return "ntc";
        case TensorLayout::Raw: return "raw";
        default: return "unknown";
    }
}

const char* QuantizationModeName(QuantizationMode mode) {
    switch (mode) {
        case QuantizationMode::None: return "none";
        case QuantizationMode::PerTensor: return "per_tensor";
        case QuantizationMode::PerAxis: return "per_axis";
        default: return "unknown";
    }
}

size_t TensorElementSize(TensorDataType type) {
    switch (type) {
        case TensorDataType::Int8:
        case TensorDataType::UInt8: return 1U;
        case TensorDataType::Int16:
        case TensorDataType::Float16: return 2U;
        case TensorDataType::Int32:
        case TensorDataType::Float32: return 4U;
        default: return 0U;
    }
}

uint64_t TensorElementCount(const TensorSpec& spec) {
    if (spec.shape.empty()) return 0;
    uint64_t count = 1;
    for (uint32_t dimension : spec.shape) {
        if (dimension == 0 || count > std::numeric_limits<uint64_t>::max() / dimension) return 0;
        count *= dimension;
    }
    return count;
}

uint64_t TensorElementCount(const TensorBuffer& tensor) {
    return TensorElementCount(tensor.spec);
}

bool TensorByteSize(const TensorSpec& spec, uint64_t& bytes, std::string& error) {
    const uint64_t elements = TensorElementCount(spec);
    const size_t element_size = TensorElementSize(spec.data_type);
    if (elements == 0) {
        error = "tensor shape must contain only positive dimensions";
        return false;
    }
    if (element_size == 0) {
        error = "tensor dtype is unsupported";
        return false;
    }
    if (elements > std::numeric_limits<uint64_t>::max() / element_size) {
        error = "tensor byte size overflows uint64";
        return false;
    }
    bytes = elements * element_size;
    if (bytes > std::numeric_limits<size_t>::max()) {
        error = "tensor byte size exceeds addressable storage";
        return false;
    }
    return true;
}

bool ValidateTensorSpec(const TensorSpec& spec, std::string& error) {
    if (spec.name.empty()) {
        error = "tensor name must not be empty";
        return false;
    }
    if (spec.shape.empty() || spec.shape.size() > 8U) {
        error = "tensor rank must be in [1,8]";
        return false;
    }
    if (!IsValidLayout(spec.layout)) {
        error = "tensor layout is unsupported";
        return false;
    }
    uint64_t ignored_bytes = 0;
    if (!TensorByteSize(spec, ignored_bytes, error)) return false;

    const QuantizationSpec& quantization = spec.quantization;
    if (quantization.mode == QuantizationMode::None) {
        if (!quantization.scales.empty() || !quantization.zero_points.empty() ||
            quantization.axis != -1) {
            error = "unquantized tensor must not define scale, zero point, or axis";
            return false;
        }
        return true;
    }
    if (quantization.scales.size() != quantization.zero_points.size() ||
        quantization.scales.empty()) {
        error = "quantization scale and zero-point arrays must be non-empty and equal length";
        return false;
    }
    for (float scale : quantization.scales) {
        if (!std::isfinite(scale) || scale <= 0.0F) {
            error = "quantization scales must be finite and positive";
            return false;
        }
    }
    if (quantization.mode == QuantizationMode::PerTensor) {
        if (quantization.scales.size() != 1U || quantization.axis != -1) {
            error = "per-tensor quantization requires one value and no axis";
            return false;
        }
        return true;
    }
    if (quantization.mode == QuantizationMode::PerAxis) {
        if (quantization.axis < 0 ||
            static_cast<size_t>(quantization.axis) >= spec.shape.size()) {
            error = "per-axis quantization axis is outside the tensor rank";
            return false;
        }
        if (quantization.scales.size() != spec.shape[static_cast<size_t>(quantization.axis)]) {
            error = "per-axis quantization length must match the selected dimension";
            return false;
        }
        return true;
    }
    error = "quantization mode is unsupported";
    return false;
}

bool ValidateTensor(const TensorBuffer& tensor, std::string& error) {
    if (!ValidateTensorSpec(tensor.spec, error)) return false;
    uint64_t bytes = 0;
    if (!TensorByteSize(tensor.spec, bytes, error)) return false;
    if (bytes != tensor.data.size()) {
        error = "tensor byte size does not match shape and dtype";
        return false;
    }
    return true;
}

bool ValidateTensor(const TensorBuffer& tensor, const TensorSpec& expected,
                    std::string& error) {
    if (!ValidateTensor(tensor, error)) return false;
    if (!TensorSpecEqual(tensor.spec, expected)) {
        error = "tensor descriptor does not match profile contract for '" + expected.name + "'";
        return false;
    }
    return true;
}

bool ValidateTensorSet(const TensorSet& tensors, const std::vector<TensorSpec>& expected,
                       std::string& error) {
    if (tensors.size() != expected.size()) {
        error = "tensor count does not match profile contract";
        return false;
    }
    std::unordered_set<std::string> names;
    for (size_t index = 0; index < tensors.size(); ++index) {
        if (!names.insert(tensors[index].spec.name).second) {
            error = "tensor names must be unique";
            return false;
        }
        if (!ValidateTensor(tensors[index], expected[index], error)) return false;
    }
    return true;
}

} // namespace npu_avs
