#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#if defined(_WIN32)
#define TEST_EXPORT __declspec(dllexport)
#else
#define TEST_EXPORT __attribute__((visibility("default")))
#endif

namespace {

constexpr int kOk = 0;
constexpr int kError = 1;
constexpr int kInt8 = 9;
constexpr size_t kMaxOptions = 256U;

struct ExternalOptions {
    const char* lib_path;
    int count;
    const char* keys[kMaxOptions];
    const char* values[kMaxOptions];
    int (*insert)(ExternalOptions*, const char*, const char*);
};

struct Model { std::string path; };
struct Options { bool delegate_added = false; };
struct Delegate {};
struct Tensor {
    int type = kInt8;
    std::vector<int32_t> shape;
    std::vector<uint8_t> data;
};
struct Interpreter {
    Tensor input;
    Tensor output;
};

size_t Elements(const std::vector<int32_t>& shape) {
    size_t result = 1U;
    for (int32_t dimension : shape) result *= static_cast<size_t>(dimension);
    return result;
}

int InsertOption(ExternalOptions* options, const char* key, const char* value) {
    if (!options || !key || !value || options->count < 0 ||
        static_cast<size_t>(options->count) >= kMaxOptions) return kError;
    options->keys[options->count] = key;
    options->values[options->count] = value;
    ++options->count;
    return kOk;
}

} // namespace

extern "C" {

TEST_EXPORT const char* TfLiteVersion() { return "fake-1.0.0"; }

TEST_EXPORT void* TfLiteModelCreateFromFile(const char* path) {
    return path ? new Model{path} : nullptr;
}

TEST_EXPORT void TfLiteModelDelete(void* model) { delete static_cast<Model*>(model); }

TEST_EXPORT void* TfLiteInterpreterOptionsCreate() { return new Options{}; }

TEST_EXPORT void TfLiteInterpreterOptionsDelete(void* options) {
    delete static_cast<Options*>(options);
}

TEST_EXPORT void TfLiteInterpreterOptionsAddDelegate(void* options, void* delegate) {
    if (options) static_cast<Options*>(options)->delegate_added = delegate != nullptr;
}

TEST_EXPORT int TfLiteInterpreterOptionsEnableCancellation(void*, bool) { return kOk; }

TEST_EXPORT void* TfLiteInterpreterCreate(const void* model_pointer, const void* options_pointer) {
    const auto* model = static_cast<const Model*>(model_pointer);
    const auto* options = static_cast<const Options*>(options_pointer);
    if (!model || !options || !options->delegate_added) return nullptr;
    auto* interpreter = new Interpreter{};
    if (model->path.find("kws01") != std::string::npos) {
        interpreter->input.shape = {1, 49, 10, 1};
        interpreter->output.shape = {1, 12};
    } else if (model->path.find("ic01") != std::string::npos) {
        interpreter->input.shape = {1, 32, 32, 3};
        interpreter->output.shape = {1, 10};
    } else if (model->path.find("ad01") != std::string::npos) {
        interpreter->input.shape = {1, 640};
        interpreter->output.shape = {1, 640};
    } else if (model->path.find("sww01") != std::string::npos) {
        interpreter->input.shape = {1, 30, 1, 40};
        interpreter->output.shape = {1, 3};
    } else {
        delete interpreter;
        return nullptr;
    }
    interpreter->input.data.resize(Elements(interpreter->input.shape));
    interpreter->output.data.resize(Elements(interpreter->output.shape));
    return interpreter;
}

TEST_EXPORT void TfLiteInterpreterDelete(void* interpreter) {
    delete static_cast<Interpreter*>(interpreter);
}

TEST_EXPORT int32_t TfLiteInterpreterGetInputTensorCount(const void*) { return 1; }
TEST_EXPORT int32_t TfLiteInterpreterGetOutputTensorCount(const void*) { return 1; }

TEST_EXPORT void* TfLiteInterpreterGetInputTensor(const void* pointer, int32_t index) {
    auto* interpreter = const_cast<Interpreter*>(static_cast<const Interpreter*>(pointer));
    return interpreter && index == 0 ? &interpreter->input : nullptr;
}

TEST_EXPORT const void* TfLiteInterpreterGetOutputTensor(const void* pointer, int32_t index) {
    const auto* interpreter = static_cast<const Interpreter*>(pointer);
    return interpreter && index == 0 ? &interpreter->output : nullptr;
}

TEST_EXPORT int TfLiteInterpreterAllocateTensors(void* interpreter) {
    return interpreter ? kOk : kError;
}

TEST_EXPORT int TfLiteInterpreterInvoke(void* pointer) {
    auto* interpreter = static_cast<Interpreter*>(pointer);
    if (!interpreter || interpreter->input.data.empty()) return kError;
    for (size_t i = 0; i < interpreter->output.data.size(); ++i) {
        interpreter->output.data[i] = static_cast<uint8_t>(
            interpreter->input.data[(i * 37U) % interpreter->input.data.size()] ^
            static_cast<uint8_t>(i));
    }
    return kOk;
}

TEST_EXPORT int TfLiteTensorType(const void* tensor) {
    return tensor ? static_cast<const Tensor*>(tensor)->type : 0;
}

TEST_EXPORT int32_t TfLiteTensorNumDims(const void* tensor) {
    return tensor ? static_cast<int32_t>(static_cast<const Tensor*>(tensor)->shape.size()) : -1;
}

TEST_EXPORT int32_t TfLiteTensorDim(const void* tensor, int32_t index) {
    const auto* typed = static_cast<const Tensor*>(tensor);
    return typed && index >= 0 && static_cast<size_t>(index) < typed->shape.size()
               ? typed->shape[static_cast<size_t>(index)] : -1;
}

TEST_EXPORT size_t TfLiteTensorByteSize(const void* tensor) {
    return tensor ? static_cast<const Tensor*>(tensor)->data.size() : 0U;
}

TEST_EXPORT int TfLiteTensorCopyFromBuffer(void* tensor, const void* data, size_t size) {
    auto* typed = static_cast<Tensor*>(tensor);
    if (!typed || !data || size != typed->data.size()) return kError;
    std::memcpy(typed->data.data(), data, size);
    return kOk;
}

TEST_EXPORT int TfLiteTensorCopyToBuffer(const void* tensor, void* data, size_t size) {
    const auto* typed = static_cast<const Tensor*>(tensor);
    if (!typed || !data || size != typed->data.size()) return kError;
    std::memcpy(data, typed->data.data(), size);
    return kOk;
}

TEST_EXPORT ExternalOptions TfLiteExternalDelegateOptionsDefault(const char* path) {
    ExternalOptions options{};
    options.lib_path = path;
    options.insert = &InsertOption;
    return options;
}

TEST_EXPORT int TfLiteExternalDelegateOptionsInsert(ExternalOptions* options,
                                                    const char* key, const char* value) {
    return InsertOption(options, key, value);
}

TEST_EXPORT void* TfLiteExternalDelegateCreate(const ExternalOptions* options) {
    return options && options->lib_path && options->lib_path[0] ? new Delegate{} : nullptr;
}

TEST_EXPORT void TfLiteExternalDelegateDelete(void* delegate) {
    delete static_cast<Delegate*>(delegate);
}

} // extern "C"
