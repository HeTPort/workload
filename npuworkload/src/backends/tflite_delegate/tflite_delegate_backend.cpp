#include "tflite_delegate_backend.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace npu_avs {
namespace {

constexpr int kTfLiteOk = 0;
constexpr int kTfLiteFloat32 = 1;
constexpr int kTfLiteUInt8 = 3;
constexpr int kTfLiteInt32 = 2;
constexpr int kTfLiteInt16 = 7;
constexpr int kTfLiteInt8 = 9;
constexpr int kTfLiteFloat16 = 10;
constexpr size_t kExternalDelegateMaxOptions = 256U;

struct ExternalDelegateOptions {
    const char* lib_path;
    int count;
    const char* keys[kExternalDelegateMaxOptions];
    const char* values[kExternalDelegateMaxOptions];
    int (*insert)(ExternalDelegateOptions*, const char*, const char*);
};

class DynamicLibrary {
public:
    DynamicLibrary() = default;
    DynamicLibrary(const DynamicLibrary&) = delete;
    DynamicLibrary& operator=(const DynamicLibrary&) = delete;

    ~DynamicLibrary() { Close(); }

    bool Open(const std::string& path, std::string& error) {
        Close();
#if defined(_WIN32)
        handle_ = LoadLibraryW(std::filesystem::u8path(path).wstring().c_str());
#else
        handle_ = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
#endif
        if (!handle_) {
            error = "failed to load TensorFlow Lite runtime library: " + path;
#if !defined(_WIN32)
            if (const char* detail = dlerror()) error += ": " + std::string(detail);
#endif
            return false;
        }
        return true;
    }

    void* Symbol(const char* name) const {
#if defined(_WIN32)
        return reinterpret_cast<void*>(GetProcAddress(handle_, name));
#else
        return dlsym(handle_, name);
#endif
    }

private:
    void Close() {
        if (!handle_) return;
#if defined(_WIN32)
        FreeLibrary(handle_);
#else
        dlclose(handle_);
#endif
        handle_ = nullptr;
    }

#if defined(_WIN32)
    HMODULE handle_ = nullptr;
#else
    void* handle_ = nullptr;
#endif
};

template <typename Function>
bool Resolve(const DynamicLibrary& library, const char* name, Function& output,
             std::string& error) {
    output = reinterpret_cast<Function>(library.Symbol(name));
    if (!output) {
        error = "TensorFlow Lite runtime is missing required symbol: " + std::string(name);
        return false;
    }
    return true;
}

int ExpectedTfLiteType(TensorDataType type) {
    switch (type) {
        case TensorDataType::Int8: return kTfLiteInt8;
        case TensorDataType::UInt8: return kTfLiteUInt8;
        case TensorDataType::Int16: return kTfLiteInt16;
        case TensorDataType::Int32: return kTfLiteInt32;
        case TensorDataType::Float16: return kTfLiteFloat16;
        case TensorDataType::Float32: return kTfLiteFloat32;
        default: return -1;
    }
}

bool ParseDelegateOptions(const std::string& text,
                          std::vector<std::pair<std::string, std::string>>& options,
                          std::string& error) {
    if (text.empty()) return true;
    size_t begin = 0;
    while (begin <= text.size()) {
        const size_t end = text.find(';', begin);
        const std::string item = text.substr(begin, end == std::string::npos
                                                        ? std::string::npos : end - begin);
        const size_t separator = item.find('=');
        if (separator == std::string::npos || separator == 0U || separator + 1U >= item.size()) {
            error = "delegate options must use non-empty key=value entries separated by ';'";
            return false;
        }
        options.emplace_back(item.substr(0, separator), item.substr(separator + 1U));
        if (options.size() > kExternalDelegateMaxOptions) {
            error = "delegate options exceed the TensorFlow Lite maximum of 256";
            return false;
        }
        if (end == std::string::npos) break;
        begin = end + 1U;
    }
    return true;
}

} // namespace

struct TfliteDelegateBackend::RuntimeState {
    using VersionFn = const char* (*)();
    using ModelCreateFn = void* (*)(const char*);
    using ModelDeleteFn = void (*)(void*);
    using OptionsCreateFn = void* (*)();
    using OptionsDeleteFn = void (*)(void*);
    using OptionsAddDelegateFn = void (*)(void*, void*);
    using OptionsEnableCancellationFn = int (*)(void*, bool);
    using InterpreterCreateFn = void* (*)(const void*, const void*);
    using InterpreterDeleteFn = void (*)(void*);
    using TensorCountFn = int32_t (*)(const void*);
    using GetInputTensorFn = void* (*)(const void*, int32_t);
    using GetOutputTensorFn = const void* (*)(const void*, int32_t);
    using AllocateFn = int (*)(void*);
    using InvokeFn = int (*)(void*);
    using TensorTypeFn = int (*)(const void*);
    using TensorNumDimsFn = int32_t (*)(const void*);
    using TensorDimFn = int32_t (*)(const void*, int32_t);
    using TensorByteSizeFn = size_t (*)(const void*);
    using TensorCopyFromFn = int (*)(void*, const void*, size_t);
    using TensorCopyToFn = int (*)(const void*, void*, size_t);
    using ExternalOptionsDefaultFn = ExternalDelegateOptions (*)(const char*);
    using ExternalOptionsInsertFn = int (*)(ExternalDelegateOptions*, const char*, const char*);
    using ExternalCreateFn = void* (*)(const ExternalDelegateOptions*);
    using ExternalDeleteFn = void (*)(void*);

    ~RuntimeState() {
        if (interpreter && interpreter_delete) interpreter_delete(interpreter);
        if (delegate && external_delete) external_delete(delegate);
        if (model && model_delete) model_delete(model);
    }

    DynamicLibrary library;
    std::mutex invoke_mutex;
    std::string version;
    void* model = nullptr;
    void* delegate = nullptr;
    void* interpreter = nullptr;
    VersionFn version_fn = nullptr;
    ModelCreateFn model_create = nullptr;
    ModelDeleteFn model_delete = nullptr;
    OptionsCreateFn options_create = nullptr;
    OptionsDeleteFn options_delete = nullptr;
    OptionsAddDelegateFn options_add_delegate = nullptr;
    OptionsEnableCancellationFn options_enable_cancellation = nullptr;
    InterpreterCreateFn interpreter_create = nullptr;
    InterpreterDeleteFn interpreter_delete = nullptr;
    TensorCountFn input_count = nullptr;
    TensorCountFn output_count = nullptr;
    GetInputTensorFn get_input = nullptr;
    GetOutputTensorFn get_output = nullptr;
    AllocateFn allocate = nullptr;
    InvokeFn invoke = nullptr;
    TensorTypeFn tensor_type = nullptr;
    TensorNumDimsFn tensor_num_dims = nullptr;
    TensorDimFn tensor_dim = nullptr;
    TensorByteSizeFn tensor_byte_size = nullptr;
    TensorCopyFromFn copy_from = nullptr;
    TensorCopyToFn copy_to = nullptr;
    ExternalOptionsDefaultFn external_options_default = nullptr;
    ExternalOptionsInsertFn external_options_insert = nullptr;
    ExternalCreateFn external_create = nullptr;
    ExternalDeleteFn external_delete = nullptr;
};

namespace {

bool ResolveApi(TfliteDelegateBackend::RuntimeState& state, std::string& error) {
    return Resolve(state.library, "TfLiteVersion", state.version_fn, error) &&
           Resolve(state.library, "TfLiteModelCreateFromFile", state.model_create, error) &&
           Resolve(state.library, "TfLiteModelDelete", state.model_delete, error) &&
           Resolve(state.library, "TfLiteInterpreterOptionsCreate", state.options_create, error) &&
           Resolve(state.library, "TfLiteInterpreterOptionsDelete", state.options_delete, error) &&
           Resolve(state.library, "TfLiteInterpreterOptionsAddDelegate", state.options_add_delegate, error) &&
           Resolve(state.library, "TfLiteInterpreterCreate", state.interpreter_create, error) &&
           Resolve(state.library, "TfLiteInterpreterDelete", state.interpreter_delete, error) &&
           Resolve(state.library, "TfLiteInterpreterGetInputTensorCount", state.input_count, error) &&
           Resolve(state.library, "TfLiteInterpreterGetOutputTensorCount", state.output_count, error) &&
           Resolve(state.library, "TfLiteInterpreterGetInputTensor", state.get_input, error) &&
           Resolve(state.library, "TfLiteInterpreterGetOutputTensor", state.get_output, error) &&
           Resolve(state.library, "TfLiteInterpreterAllocateTensors", state.allocate, error) &&
           Resolve(state.library, "TfLiteInterpreterInvoke", state.invoke, error) &&
           Resolve(state.library, "TfLiteTensorType", state.tensor_type, error) &&
           Resolve(state.library, "TfLiteTensorNumDims", state.tensor_num_dims, error) &&
           Resolve(state.library, "TfLiteTensorDim", state.tensor_dim, error) &&
           Resolve(state.library, "TfLiteTensorByteSize", state.tensor_byte_size, error) &&
           Resolve(state.library, "TfLiteTensorCopyFromBuffer", state.copy_from, error) &&
           Resolve(state.library, "TfLiteTensorCopyToBuffer", state.copy_to, error) &&
           Resolve(state.library, "TfLiteExternalDelegateOptionsDefault",
                   state.external_options_default, error) &&
           Resolve(state.library, "TfLiteExternalDelegateOptionsInsert",
                   state.external_options_insert, error) &&
           Resolve(state.library, "TfLiteExternalDelegateCreate", state.external_create, error) &&
           Resolve(state.library, "TfLiteExternalDelegateDelete", state.external_delete, error);
}

bool ValidateRuntimeTensor(const TfliteDelegateBackend::RuntimeState& state,
                           const void* tensor, const TensorSpec& expected,
                           const std::string& kind, size_t index, std::string& error) {
    if (!tensor) {
        error = "TensorFlow Lite returned a null " + kind + " tensor";
        return false;
    }
    if (state.tensor_type(tensor) != ExpectedTfLiteType(expected.data_type)) {
        error = kind + " tensor " + std::to_string(index) + " dtype does not match the profile";
        return false;
    }
    const int32_t rank = state.tensor_num_dims(tensor);
    if (rank < 0 || static_cast<size_t>(rank) != expected.shape.size()) {
        error = kind + " tensor " + std::to_string(index) + " rank does not match the profile";
        return false;
    }
    for (int32_t dimension = 0; dimension < rank; ++dimension) {
        if (state.tensor_dim(tensor, dimension) !=
            static_cast<int32_t>(expected.shape[static_cast<size_t>(dimension)])) {
            error = kind + " tensor " + std::to_string(index) + " shape does not match the profile";
            return false;
        }
    }
    uint64_t expected_bytes = 0;
    if (!TensorByteSize(expected, expected_bytes, error)) return false;
    if (state.tensor_byte_size(tensor) != expected_bytes) {
        error = kind + " tensor " + std::to_string(index) + " byte size does not match the profile";
        return false;
    }
    return true;
}

} // namespace

TfliteDelegateBackend::TfliteDelegateBackend() = default;

TfliteDelegateBackend::~TfliteDelegateBackend() {
    std::string ignored;
    (void)Destroy(50U, ignored);
}

bool TfliteDelegateBackend::SupportsProfile(const ProfileSpec& profile) const {
    return profile.implemented && profile.model_format == "tflite" &&
           !profile.inputs.empty() && !profile.outputs.empty();
}

BackendStatus TfliteDelegateBackend::Init(const WorkloadConfig& cfg,
                                          const ProfileSpec& profile,
                                          std::string& error) {
    if (!SupportsProfile(profile)) {
        error = "tflite_delegate backend does not support profile '" + profile.id + "'";
        return BackendStatus::Error;
    }
    Configure(cfg, profile);
    if (profile.model_path.empty()) {
        error = "a checked TFLite model must be supplied by the profile manifest";
        return BackendStatus::Error;
    }
    if (cfg.runtime_library.empty() || cfg.delegate_library.empty()) {
        error = "tflite_delegate requires --runtime-library and --delegate-library";
        return BackendStatus::Error;
    }
    std::vector<std::pair<std::string, std::string>> ignored;
    if (!ParseDelegateOptions(cfg.delegate_options, ignored, error)) return BackendStatus::Error;
    return BackendStatus::Ok;
}

BackendStatus TfliteDelegateBackend::CreateResources(std::string& error) {
    auto state = std::make_shared<RuntimeState>();
    if (!state->library.Open(cfg_.runtime_library, error) || !ResolveApi(*state, error)) {
        return BackendStatus::Error;
    }
    state->options_enable_cancellation =
        reinterpret_cast<RuntimeState::OptionsEnableCancellationFn>(
            state->library.Symbol("TfLiteInterpreterOptionsEnableCancellation"));
    state->version = state->version_fn() ? state->version_fn() : "unknown";
    state->model = state->model_create(profile_.model_path.c_str());
    if (!state->model) {
        error = "TensorFlow Lite failed to load the checked model";
        return BackendStatus::Error;
    }

    std::vector<std::pair<std::string, std::string>> delegate_options;
    if (!ParseDelegateOptions(cfg_.delegate_options, delegate_options, error)) {
        return BackendStatus::Error;
    }
    ExternalDelegateOptions external =
        state->external_options_default(cfg_.delegate_library.c_str());
    for (const auto& option : delegate_options) {
        if (state->external_options_insert(&external, option.first.c_str(),
                                           option.second.c_str()) != kTfLiteOk) {
            error = "TensorFlow Lite rejected external-delegate option '" + option.first + "'";
            return BackendStatus::Error;
        }
    }
    state->delegate = state->external_create(&external);
    if (!state->delegate) {
        error = "TensorFlow Lite failed to create the external NPU delegate";
        return BackendStatus::Error;
    }

    void* options = state->options_create();
    if (!options) {
        error = "TensorFlow Lite failed to create interpreter options";
        return BackendStatus::AllocationFail;
    }
    state->options_add_delegate(options, state->delegate);
    if (state->options_enable_cancellation) {
        (void)state->options_enable_cancellation(options, true);
    }
    state->interpreter = state->interpreter_create(state->model, options);
    state->options_delete(options);
    if (!state->interpreter) {
        error = "TensorFlow Lite failed to create an interpreter with the external delegate";
        return BackendStatus::Error;
    }
    if (state->allocate(state->interpreter) != kTfLiteOk) {
        error = "TensorFlow Lite failed to allocate delegated tensors";
        return BackendStatus::AllocationFail;
    }
    if (state->input_count(state->interpreter) != static_cast<int32_t>(profile_.inputs.size()) ||
        state->output_count(state->interpreter) != static_cast<int32_t>(profile_.outputs.size())) {
        error = "TensorFlow Lite model tensor counts do not match the profile";
        return BackendStatus::Error;
    }
    for (size_t i = 0; i < profile_.inputs.size(); ++i) {
        if (!ValidateRuntimeTensor(*state, state->get_input(state->interpreter,
                                   static_cast<int32_t>(i)), profile_.inputs[i],
                                   "input", i, error)) return BackendStatus::Error;
    }
    for (size_t i = 0; i < profile_.outputs.size(); ++i) {
        if (!ValidateRuntimeTensor(*state, state->get_output(state->interpreter,
                                   static_cast<int32_t>(i)), profile_.outputs[i],
                                   "output", i, error)) return BackendStatus::Error;
    }
    runtime_ = std::move(state);
    return BackendStatus::Ok;
}

AsyncBackendBase::WorkTask TfliteDelegateBackend::MakeTask(
    const TensorSet& inputs, uint64_t inference_index) const {
    (void)inference_index;
    const std::shared_ptr<RuntimeState> state = runtime_;
    const std::vector<TensorSpec> outputs = profile_.outputs;
    return [state, inputs, outputs](const std::atomic<bool>& cancelled) {
        WorkResult work;
        if (!state) {
            work.status = BackendStatus::Error;
            work.error = "TensorFlow Lite resources are not initialized";
            return work;
        }
        std::lock_guard<std::mutex> lock(state->invoke_mutex);
        if (cancelled.load(std::memory_order_relaxed)) {
            work.status = BackendStatus::Timeout;
            work.error = "delegated inference cancelled before invocation";
            return work;
        }
        for (size_t i = 0; i < inputs.size(); ++i) {
            void* tensor = state->get_input(state->interpreter, static_cast<int32_t>(i));
            if (state->copy_from(tensor, inputs[i].data.data(), inputs[i].data.size()) != kTfLiteOk) {
                work.status = BackendStatus::Error;
                work.error = "failed to copy profile input into TensorFlow Lite";
                return work;
            }
        }
        const int status = state->invoke(state->interpreter);
        if (status != kTfLiteOk) {
            work.status = status == 8 ? BackendStatus::Timeout : BackendStatus::Error;
            work.error = "TensorFlow Lite external-delegate invocation failed with status " +
                         std::to_string(status);
            return work;
        }
        if (cancelled.load(std::memory_order_relaxed)) {
            work.status = BackendStatus::Timeout;
            work.error = "delegated inference exceeded its completion deadline";
            return work;
        }
        for (size_t i = 0; i < outputs.size(); ++i) {
            TensorBuffer output;
            output.spec = outputs[i];
            output.data.resize(state->tensor_byte_size(
                state->get_output(state->interpreter, static_cast<int32_t>(i))));
            if (state->copy_to(state->get_output(state->interpreter, static_cast<int32_t>(i)),
                               output.data.data(), output.data.size()) != kTfLiteOk) {
                work.status = BackendStatus::Error;
                work.error = "failed to copy TensorFlow Lite output into the profile tensor";
                return work;
            }
            work.inference.outputs.push_back(std::move(output));
        }
        work.inference.operation_count = 1U;
        work.inference.device_time_ms = 0.0;
        work.inference.device_time_valid = false;
        return work;
    };
}

BackendExecutionInfo TfliteDelegateBackend::GetExecutionInfo() const {
    const std::string runtime = runtime_ ? "tensorflow_lite_" + runtime_->version
                                         : "tensorflow_lite_external_delegate";
    return {runtime, "external_npu_delegate", {profile_.id + ":external_delegate"}, false};
}

BackendStatus TfliteDelegateBackend::Destroy(uint32_t timeout_ms, std::string& error) {
    const BackendStatus status = AsyncBackendBase::Destroy(timeout_ms, error);
    runtime_.reset();
    return status;
}

} // namespace npu_avs
