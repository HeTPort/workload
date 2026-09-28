#include "npu_avs/tensor.h"

#include <limits>

namespace npu_avs {

const char* TensorDataTypeName(TensorDataType type) {
    switch (type) {
        case TensorDataType::Int8: return "int8";
        case TensorDataType::UInt8: return "uint8";
        case TensorDataType::Float32: return "float32";
        default: return "unknown";
    }
}

size_t TensorElementSize(TensorDataType type) {
    return type == TensorDataType::Float32 ? sizeof(float) : sizeof(uint8_t);
}

uint64_t TensorElementCount(const TensorBuffer& tensor) {
    if (tensor.shape.empty()) return 0;
    uint64_t count = 1;
    for (uint32_t dimension : tensor.shape) {
        if (dimension == 0 || count > std::numeric_limits<uint64_t>::max() / dimension) return 0;
        count *= dimension;
    }
    return count;
}

bool ValidateTensor(const TensorBuffer& tensor, std::string& error) {
    const uint64_t elements = TensorElementCount(tensor);
    if (elements == 0) {
        error = "tensor shape must contain only positive dimensions";
        return false;
    }
    const uint64_t bytes = elements * TensorElementSize(tensor.data_type);
    if (bytes != tensor.data.size()) {
        error = "tensor byte size does not match shape and dtype";
        return false;
    }
    return true;
}

} // namespace npu_avs
