#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace npu_avs {

enum class TensorDataType { Int8, UInt8, Int16, Int32, Float16, Float32 };
enum class TensorLayout { Scalar, NC, NCHW, NHWC, NTC, Raw };
enum class QuantizationMode { None, PerTensor, PerAxis };

struct QuantizationSpec {
    QuantizationMode mode = QuantizationMode::None;
    std::vector<float> scales;
    std::vector<int32_t> zero_points;
    int32_t axis = -1;
};

struct TensorSpec {
    std::string name;
    TensorDataType data_type = TensorDataType::Int8;
    std::vector<uint32_t> shape;
    TensorLayout layout = TensorLayout::Raw;
    QuantizationSpec quantization;
};

struct TensorBuffer {
    TensorSpec spec;
    std::vector<uint8_t> data;
};

using TensorSet = std::vector<TensorBuffer>;

const char* TensorDataTypeName(TensorDataType type);
const char* TensorLayoutName(TensorLayout layout);
const char* QuantizationModeName(QuantizationMode mode);
size_t TensorElementSize(TensorDataType type);
uint64_t TensorElementCount(const TensorSpec& spec);
uint64_t TensorElementCount(const TensorBuffer& tensor);
bool TensorByteSize(const TensorSpec& spec, uint64_t& bytes, std::string& error);
bool ValidateTensorSpec(const TensorSpec& spec, std::string& error);
bool ValidateTensor(const TensorBuffer& tensor, std::string& error);
bool ValidateTensor(const TensorBuffer& tensor, const TensorSpec& expected, std::string& error);
bool ValidateTensorSet(const TensorSet& tensors, const std::vector<TensorSpec>& expected,
                       std::string& error);

} // namespace npu_avs
