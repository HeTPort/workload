#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace npu_avs {

enum class TensorDataType { Int8, UInt8, Float32 };

struct TensorBuffer {
    std::string name;
    TensorDataType data_type = TensorDataType::Int8;
    std::vector<uint32_t> shape;
    float scale = 1.0F;
    int32_t zero_point = 0;
    std::vector<uint8_t> data;
};

const char* TensorDataTypeName(TensorDataType type);
size_t TensorElementSize(TensorDataType type);
uint64_t TensorElementCount(const TensorBuffer& tensor);
bool ValidateTensor(const TensorBuffer& tensor, std::string& error);

} // namespace npu_avs
