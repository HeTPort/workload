#include "opencl_backend.h"

#include "gpu_avs/utils.h"

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <vector>

namespace gpu_avs {

OpenClBackend::~OpenClBackend() {
    Destroy();
}

std::string OpenClBackend::ClErrorToString(cl_int err) const {
    switch (err) {
        case CL_SUCCESS: return "CL_SUCCESS";
        case CL_DEVICE_NOT_FOUND: return "CL_DEVICE_NOT_FOUND";
        case CL_DEVICE_NOT_AVAILABLE: return "CL_DEVICE_NOT_AVAILABLE";
        case CL_COMPILER_NOT_AVAILABLE: return "CL_COMPILER_NOT_AVAILABLE";
        case CL_MEM_OBJECT_ALLOCATION_FAILURE: return "CL_MEM_OBJECT_ALLOCATION_FAILURE";
        case CL_OUT_OF_RESOURCES: return "CL_OUT_OF_RESOURCES";
        case CL_OUT_OF_HOST_MEMORY: return "CL_OUT_OF_HOST_MEMORY";
        case CL_PROFILING_INFO_NOT_AVAILABLE: return "CL_PROFILING_INFO_NOT_AVAILABLE";
        case CL_MEM_COPY_OVERLAP: return "CL_MEM_COPY_OVERLAP";
        case CL_IMAGE_FORMAT_MISMATCH: return "CL_IMAGE_FORMAT_MISMATCH";
        case CL_IMAGE_FORMAT_NOT_SUPPORTED: return "CL_IMAGE_FORMAT_NOT_SUPPORTED";
        case CL_BUILD_PROGRAM_FAILURE: return "CL_BUILD_PROGRAM_FAILURE";
        case CL_MAP_FAILURE: return "CL_MAP_FAILURE";
        case CL_INVALID_VALUE: return "CL_INVALID_VALUE";
        case CL_INVALID_DEVICE_TYPE: return "CL_INVALID_DEVICE_TYPE";
        case CL_INVALID_PLATFORM: return "CL_INVALID_PLATFORM";
        case CL_INVALID_DEVICE: return "CL_INVALID_DEVICE";
        case CL_INVALID_CONTEXT: return "CL_INVALID_CONTEXT";
        case CL_INVALID_QUEUE_PROPERTIES: return "CL_INVALID_QUEUE_PROPERTIES";
        case CL_INVALID_COMMAND_QUEUE: return "CL_INVALID_COMMAND_QUEUE";
        case CL_INVALID_HOST_PTR: return "CL_INVALID_HOST_PTR";
        case CL_INVALID_MEM_OBJECT: return "CL_INVALID_MEM_OBJECT";
        case CL_INVALID_IMAGE_FORMAT_DESCRIPTOR: return "CL_INVALID_IMAGE_FORMAT_DESCRIPTOR";
        case CL_INVALID_IMAGE_SIZE: return "CL_INVALID_IMAGE_SIZE";
        case CL_INVALID_SAMPLER: return "CL_INVALID_SAMPLER";
        case CL_INVALID_BINARY: return "CL_INVALID_BINARY";
        case CL_INVALID_BUILD_OPTIONS: return "CL_INVALID_BUILD_OPTIONS";
        case CL_INVALID_PROGRAM: return "CL_INVALID_PROGRAM";
        case CL_INVALID_PROGRAM_EXECUTABLE: return "CL_INVALID_PROGRAM_EXECUTABLE";
        case CL_INVALID_KERNEL_NAME: return "CL_INVALID_KERNEL_NAME";
        case CL_INVALID_KERNEL_DEFINITION: return "CL_INVALID_KERNEL_DEFINITION";
        case CL_INVALID_KERNEL: return "CL_INVALID_KERNEL";
        case CL_INVALID_ARG_INDEX: return "CL_INVALID_ARG_INDEX";
        case CL_INVALID_ARG_VALUE: return "CL_INVALID_ARG_VALUE";
        case CL_INVALID_ARG_SIZE: return "CL_INVALID_ARG_SIZE";
        case CL_INVALID_KERNEL_ARGS: return "CL_INVALID_KERNEL_ARGS";
        case CL_INVALID_WORK_DIMENSION: return "CL_INVALID_WORK_DIMENSION";
        case CL_INVALID_WORK_GROUP_SIZE: return "CL_INVALID_WORK_GROUP_SIZE";
        case CL_INVALID_WORK_ITEM_SIZE: return "CL_INVALID_WORK_ITEM_SIZE";
        case CL_INVALID_GLOBAL_OFFSET: return "CL_INVALID_GLOBAL_OFFSET";
        case CL_INVALID_EVENT_WAIT_LIST: return "CL_INVALID_EVENT_WAIT_LIST";
        case CL_INVALID_EVENT: return "CL_INVALID_EVENT";
        case CL_INVALID_OPERATION: return "CL_INVALID_OPERATION";
        case CL_INVALID_GL_OBJECT: return "CL_INVALID_GL_OBJECT";
        case CL_INVALID_BUFFER_SIZE: return "CL_INVALID_BUFFER_SIZE";
        case CL_INVALID_MIP_LEVEL: return "CL_INVALID_MIP_LEVEL";
        case CL_INVALID_GLOBAL_WORK_SIZE: return "CL_INVALID_GLOBAL_WORK_SIZE";
        default: {
            std::ostringstream os;
            os << "CL_UNKNOWN_ERROR(" << err << ")";
            return os.str();
        }
    }
}

void OpenClBackend::SetStatusFromClError(cl_int err) {
    if (err == CL_SUCCESS) {
        last_status_ = SubmitStatus::Ok;
    } else if (err == CL_DEVICE_NOT_AVAILABLE) {
        last_status_ = SubmitStatus::DeviceLost;
    } else if (err == CL_MEM_OBJECT_ALLOCATION_FAILURE ||
               err == CL_OUT_OF_HOST_MEMORY ||
               err == CL_OUT_OF_RESOURCES ||
               err == CL_MAP_FAILURE ||
               err == CL_INVALID_BUFFER_SIZE) {
        last_status_ = SubmitStatus::AllocationFail;
    } else {
        last_status_ = SubmitStatus::ApiError;
    }
}

bool OpenClBackend::Init(const WorkloadConfig& cfg, std::string& error) {
    cfg_ = cfg;
    last_status_ = SubmitStatus::Ok;

    if (cfg_.mode != "compute") {
        error = "OpenCL backend supports only mode=compute";
        last_status_ = SubmitStatus::ApiError;
        return false;
    }

    if (cfg_.width == 0 || cfg_.height == 0) {
        error = "invalid resolution";
        last_status_ = SubmitStatus::ApiError;
        return false;
    }

    if (cfg_.rt_format != "RGBA8") {
        error = "OpenCL backend supports only rt_format=RGBA8";
        last_status_ = SubmitStatus::ApiError;
        return false;
    }

    if (!PickPlatformAndDevice(error)) {
        return false;
    }

    if (!CreateContextAndQueue(error)) {
        return false;
    }

    return true;
}

bool OpenClBackend::CreateResources(std::string& error) {
    if (!CreateOutputBuffer(error)) {
        return false;
    }

    if (!BuildProgramAndKernel(error)) {
        return false;
    }

    resources_created_ = true;
    return true;
}

bool OpenClBackend::PickPlatformAndDevice(std::string& error) {
    cl_uint platform_count = 0;
    cl_int err = clGetPlatformIDs(0, nullptr, &platform_count);

    if (err != CL_SUCCESS || platform_count == 0) {
        SetStatusFromClError(err);
        error = "clGetPlatformIDs failed or no platform: " + ClErrorToString(err);
        return false;
    }

    std::vector<cl_platform_id> platforms(platform_count);

    err = clGetPlatformIDs(platform_count, platforms.data(), nullptr);
    if (err != CL_SUCCESS) {
        SetStatusFromClError(err);
        error = "clGetPlatformIDs failed: " + ClErrorToString(err);
        return false;
    }

    // Prefer GPU device.
    for (auto p : platforms) {
        cl_uint device_count = 0;
        err = clGetDeviceIDs(p, CL_DEVICE_TYPE_GPU, 0, nullptr, &device_count);

        if (err == CL_SUCCESS && device_count > 0) {
            std::vector<cl_device_id> devices(device_count);

            err = clGetDeviceIDs(
                p,
                CL_DEVICE_TYPE_GPU,
                device_count,
                devices.data(),
                nullptr
            );

            if (err == CL_SUCCESS && !devices.empty()) {
                platform_ = p;
                device_ = devices[0];
                return true;
            }
        }
    }

    // Fallback to any device.
    for (auto p : platforms) {
        cl_uint device_count = 0;
        err = clGetDeviceIDs(p, CL_DEVICE_TYPE_ALL, 0, nullptr, &device_count);

        if (err == CL_SUCCESS && device_count > 0) {
            std::vector<cl_device_id> devices(device_count);

            err = clGetDeviceIDs(
                p,
                CL_DEVICE_TYPE_ALL,
                device_count,
                devices.data(),
                nullptr
            );

            if (err == CL_SUCCESS && !devices.empty()) {
                platform_ = p;
                device_ = devices[0];
                return true;
            }
        }
    }

    error = "no OpenCL device found";
    last_status_ = SubmitStatus::ApiError;
    return false;
}

bool OpenClBackend::CreateContextAndQueue(std::string& error) {
    cl_int err = CL_SUCCESS;

    context_ = clCreateContext(
        nullptr,
        1,
        &device_,
        nullptr,
        nullptr,
        &err
    );

    if (err != CL_SUCCESS || !context_) {
        SetStatusFromClError(err);
        error = "clCreateContext failed: " + ClErrorToString(err);
        return false;
    }

    profiling_supported_ = true;

#if CL_TARGET_OPENCL_VERSION >= 200
    const cl_queue_properties queue_props[] = {
        CL_QUEUE_PROPERTIES,
        static_cast<cl_queue_properties>(CL_QUEUE_PROFILING_ENABLE),
        0
    };

    queue_ = clCreateCommandQueueWithProperties(
        context_,
        device_,
        queue_props,
        &err
    );
#else
    queue_ = clCreateCommandQueue(
        context_,
        device_,
        CL_QUEUE_PROFILING_ENABLE,
        &err
    );
#endif

    if (err != CL_SUCCESS || !queue_) {
        profiling_supported_ = false;

#if CL_TARGET_OPENCL_VERSION >= 200
        const cl_queue_properties fallback_props[] = {
            CL_QUEUE_PROPERTIES,
            0,
            0
        };

        queue_ = clCreateCommandQueueWithProperties(
            context_,
            device_,
            fallback_props,
            &err
        );
#else
        queue_ = clCreateCommandQueue(
            context_,
            device_,
            0,
            &err
        );
#endif

        if (err != CL_SUCCESS || !queue_) {
            SetStatusFromClError(err);
            error = "clCreateCommandQueue failed: " + ClErrorToString(err);
            return false;
        }
    }

    return true;
}

bool OpenClBackend::CreateOutputBuffer(std::string& error) {
    output_size_ =
        static_cast<size_t>(cfg_.width) *
        static_cast<size_t>(cfg_.height) *
        4u;

    host_output_.resize(output_size_);

    cl_int err = CL_SUCCESS;

    output_buffer_ = clCreateBuffer(
        context_,
        CL_MEM_READ_WRITE,
        output_size_,
        nullptr,
        &err
    );

    if (err != CL_SUCCESS || !output_buffer_) {
        SetStatusFromClError(err);
        error = "clCreateBuffer output failed: " + ClErrorToString(err);
        return false;
    }

    return true;
}

static bool IsAbsolutePathOpenCl(const std::string& path) {
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

static std::string JoinPathOpenCl(
    const std::string& dir,
    const std::string& file
) {
    if (dir.empty()) {
        return file;
    }

    char last = dir.back();
    if (last == '/' || last == '\\') {
        return dir + file;
    }

    return dir + "/" + file;
}

bool OpenClBackend::LoadKernelSource(
    std::string& source,
    std::string& error
) const {
    source.clear();

    std::vector<std::string> candidates;
    const std::string filename = "workload.cl";

    if (!cfg_.shader_dir.empty()) {
        if (IsAbsolutePathOpenCl(cfg_.shader_dir)) {
            candidates.push_back(JoinPathOpenCl(cfg_.shader_dir, filename));
        } else {
            candidates.push_back(JoinPathOpenCl(cfg_.shader_dir, filename));
        }
    }

    const char* env_dir = std::getenv("GPU_AVS_SHADER_DIR");
    if (env_dir && env_dir[0]) {
        candidates.push_back(JoinPathOpenCl(env_dir, filename));
    }

    candidates.push_back("./shaders/opencl/workload.cl");
    candidates.push_back("/data/local/tmp/shaders/opencl/workload.cl");

    for (const auto& path : candidates) {
        std::ifstream in(path);
        if (!in) {
            continue;
        }

        std::ostringstream ss;
        ss << in.rdbuf();
        source = ss.str();

        if (!source.empty()) {
            return true;
        }

        error = "OpenCL kernel file is empty: " + path;
        return false;
    }

    // Fallback to builtin kernel, useful for single-binary deployment.
    source = BuiltinKernelSource();
    return !source.empty();
}

std::string OpenClBackend::BuiltinKernelSource() const {
    return R"CLC(
float fract_f(float x) {
    return x - floor(x);
}

float2 fract2(float2 x) {
    return x - floor(x);
}

float3 fract3(float3 x) {
    return x - floor(x);
}

float4 fract4(float4 x) {
    return x - floor(x);
}

uint hash_u32(uint x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

uint pack_rgba(float4 c) {
    uint r = (uint)(clamp(c.x, 0.0f, 1.0f) * 255.0f);
    uint g = (uint)(clamp(c.y, 0.0f, 1.0f) * 255.0f);
    uint b = (uint)(clamp(c.z, 0.0f, 1.0f) * 255.0f);
    uint a = (uint)(clamp(c.w, 0.0f, 1.0f) * 255.0f);

    return r | (g << 8) | (b << 16) | (a << 24);
}

float4 workload_alu(float2 uv, uint iterations) {
    float4 x = (float4)(uv.x, uv.y, 0.37f, 1.0f);

    for (uint i = 0; i < iterations; ++i) {
        x = x * (float4)(1.00013f, 0.99991f, 1.00007f, 0.99989f)
              + (float4)(0.00031f, 0.00017f, 0.00023f, 0.00029f);
        x = fract4(x * 1.61803398875f + x.yzwx);
    }

    return (float4)(x.x, x.y, x.z, 1.0f);
}

float4 workload_sfu(float2 uv, uint iterations) {
    float2 p = uv * 6.2831853f;
    float x = p.x + 0.17f;
    float y = p.y + 0.31f;

    for (uint i = 0; i < iterations; ++i) {
        x = sin(x) + cos(y) + 0.001f;
        y = cos(y) - sin(x) + 0.002f;
    }

    return (float4)(
        fract_f(fabs(x)),
        fract_f(fabs(y)),
        fract_f(fabs(x + y)),
        1.0f
    );
}

float4 workload_texture_like(
    uint gx,
    uint gy,
    float2 uv,
    uint iterations,
    uint texture_count,
    uint frame_index
) {
    (void)uv;

    float4 c = (float4)(0.0f);
    uint count = max(texture_count, 1u);

    for (uint i = 0; i < iterations; ++i) {
        uint h = hash_u32(
            gx * 73856093u ^
            gy * 19349663u ^
            i * 83492791u ^
            frame_index * 2654435761u
        );

        float3 v = (float3)(
            (float)((h >> 0) & 255u),
            (float)((h >> 8) & 255u),
            (float)((h >> 16) & 255u)
        ) / 255.0f;

        for (uint t = 0; t < count; ++t) {
            uint ht = hash_u32(h ^ t * 2246822519u);

            c += (float4)(
                (float)((ht >> 0) & 255u) / 255.0f,
                (float)((ht >> 8) & 255u) / 255.0f,
                (float)((ht >> 16) & 255u) / 255.0f,
                1.0f
            ) * 0.5f + (float4)(v.x, v.y, v.z, 1.0f) * 0.5f;
        }
    }

    c /= (float)max(iterations * count, 1u);
    return (float4)(c.x, c.y, c.z, 1.0f);
}

float4 workload_fill(float2 uv) {
    return (float4)(uv.x, uv.y, fract_f(uv.x + uv.y), 1.0f);
}

float4 workload_mixed(
    uint gx,
    uint gy,
    float2 uv,
    uint iterations,
    uint texture_count,
    uint frame_index
) {
    float4 x = (float4)(uv.x, uv.y, 0.37f, 1.0f);
    float4 tex = (float4)(0.0f);
    uint count = max(texture_count, 1u);

    for (uint i = 0; i < iterations; ++i) {
        x = x * (float4)(1.00011f, 0.99997f, 1.00005f, 0.99993f)
              + (float4)(0.00019f, 0.00023f, 0.00029f, 0.00031f);

        uint h = hash_u32(
            gx * 73856093u ^
            gy * 19349663u ^
            i * 83492791u ^
            frame_index * 2654435761u
        );

        for (uint t = 0; t < count; ++t) {
            uint ht = hash_u32(h ^ t * 2246822519u);

            tex += (float4)(
                (float)((ht >> 0) & 255u) / 255.0f,
                (float)((ht >> 8) & 255u) / 255.0f,
                (float)((ht >> 16) & 255u) / 255.0f,
                1.0f
            );
        }

        x = fract4(x + tex * 0.0007f + x.yzwx * 0.013f);
    }

    float3 rgb = fract3(
        x.xyz +
        tex.xyz / (float)max(iterations * count, 1u)
    );

    return (float4)(rgb.x, rgb.y, rgb.z, 1.0f);
}

__kernel void gpu_avs_workload(
    __global uint* out_buf,
    uint width,
    uint height,
    uint iterations,
    uint shader_id,
    uint texture_count,
    uint frame_index
) {
    uint gx = get_global_id(0);
    uint gy = get_global_id(1);

    if (gx >= width || gy >= height) {
        return;
    }

    uint index = gy * width + gx;

    float2 uv = (float2)(
        (float)gx / (float)max(width, 1u),
        (float)gy / (float)max(height, 1u)
    );

    float4 color;

    if (shader_id == 1u) {
        color = workload_alu(uv, iterations);
    } else if (shader_id == 2u) {
        color = workload_sfu(uv, iterations);
    } else if (shader_id == 3u) {
        color = workload_texture_like(
            gx,
            gy,
            uv,
            iterations,
            texture_count,
            frame_index
        );
    } else if (shader_id == 4u) {
        color = workload_fill(uv);
    } else {
        color = workload_mixed(
            gx,
            gy,
            uv,
            iterations,
            texture_count,
            frame_index
        );
    }

    out_buf[index] = pack_rgba(color);
}
)CLC";
}

bool OpenClBackend::BuildProgramAndKernel(std::string& error) {
    std::string source;

    if (!LoadKernelSource(source, error)) {
        last_status_ = SubmitStatus::ApiError;
        return false;
    }

    const char* src = source.c_str();
    size_t len = source.size();

    cl_int err = CL_SUCCESS;

    program_ = clCreateProgramWithSource(
        context_,
        1,
        &src,
        &len,
        &err
    );

    if (err != CL_SUCCESS || !program_) {
        SetStatusFromClError(err);
        error = "clCreateProgramWithSource failed: " + ClErrorToString(err);
        return false;
    }

    const char* options = "";

    err = clBuildProgram(
        program_,
        1,
        &device_,
        options,
        nullptr,
        nullptr
    );

    if (err != CL_SUCCESS) {
        size_t log_size = 0;

        clGetProgramBuildInfo(
            program_,
            device_,
            CL_PROGRAM_BUILD_LOG,
            0,
            nullptr,
            &log_size
        );

        std::string log;
        log.resize(log_size);

        if (log_size > 0) {
            clGetProgramBuildInfo(
                program_,
                device_,
                CL_PROGRAM_BUILD_LOG,
                log_size,
                log.data(),
                nullptr
            );
        }

        SetStatusFromClError(err);

        std::ostringstream os;
        os << "clBuildProgram failed: "
           << ClErrorToString(err)
           << ", build_log="
           << log;

        error = os.str();
        return false;
    }

    kernel_ = clCreateKernel(
        program_,
        "gpu_avs_workload",
        &err
    );

    if (err != CL_SUCCESS || !kernel_) {
        SetStatusFromClError(err);
        error = "clCreateKernel failed: " + ClErrorToString(err);
        return false;
    }

    return true;
}

uint32_t OpenClBackend::ShaderId() const {
    if (cfg_.shader == "alu") {
        return 1;
    }

    if (cfg_.shader == "sfu") {
        return 2;
    }

    if (cfg_.shader == "texture") {
        return 3;
    }

    if (cfg_.shader == "fill") {
        return 4;
    }

    return 5;
}

SubmitStatus OpenClBackend::SubmitWorkload(uint64_t frame_index) {
    last_status_ = SubmitStatus::Ok;

    if (!resources_created_) {
        last_status_ = SubmitStatus::AllocationFail;
        return last_status_;
    }

    if (submitted_ && last_event_) {
        clWaitForEvents(1, &last_event_);
        clReleaseEvent(last_event_);
        last_event_ = nullptr;
        submitted_ = false;
    }

    cl_int err = CL_SUCCESS;

    uint32_t width = cfg_.width;
    uint32_t height = cfg_.height;
    uint32_t iterations = cfg_.iterations;
    uint32_t shader_id = ShaderId();
    uint32_t texture_count = cfg_.texture_count;
    uint32_t frame_u32 = static_cast<uint32_t>(frame_index);

    err = clSetKernelArg(kernel_, 0, sizeof(cl_mem), &output_buffer_);
    if (err == CL_SUCCESS) err = clSetKernelArg(kernel_, 1, sizeof(uint32_t), &width);
    if (err == CL_SUCCESS) err = clSetKernelArg(kernel_, 2, sizeof(uint32_t), &height);
    if (err == CL_SUCCESS) err = clSetKernelArg(kernel_, 3, sizeof(uint32_t), &iterations);
    if (err == CL_SUCCESS) err = clSetKernelArg(kernel_, 4, sizeof(uint32_t), &shader_id);
    if (err == CL_SUCCESS) err = clSetKernelArg(kernel_, 5, sizeof(uint32_t), &texture_count);
    if (err == CL_SUCCESS) err = clSetKernelArg(kernel_, 6, sizeof(uint32_t), &frame_u32);

    if (err != CL_SUCCESS) {
        SetStatusFromClError(err);
        return last_status_;
    }

    const size_t local[2] = {16, 16};

    const size_t global[2] = {
        ((static_cast<size_t>(cfg_.width) + local[0] - 1) / local[0]) * local[0],
        ((static_cast<size_t>(cfg_.height) + local[1] - 1) / local[1]) * local[1]
    };

    err = clEnqueueNDRangeKernel(
        queue_,
        kernel_,
        2,
        nullptr,
        global,
        local,
        0,
        nullptr,
        &last_event_
    );

    if (err != CL_SUCCESS) {
        SetStatusFromClError(err);
        return last_status_;
    }

    err = clFlush(queue_);
    if (err != CL_SUCCESS) {
        SetStatusFromClError(err);
        return last_status_;
    }

    submitted_ = true;
    return SubmitStatus::Ok;
}

bool OpenClBackend::IsEventComplete(
    cl_event event,
    cl_int& exec_status,
    std::string& error
) {
    cl_int err = clGetEventInfo(
        event,
        CL_EVENT_COMMAND_EXECUTION_STATUS,
        sizeof(exec_status),
        &exec_status,
        nullptr
    );

    if (err != CL_SUCCESS) {
        SetStatusFromClError(err);
        error = "clGetEventInfo failed: " + ClErrorToString(err);
        return false;
    }

    return exec_status == CL_COMPLETE;
}

bool OpenClBackend::WaitIdleOrFrameDone(
    uint64_t timeout_ns,
    std::string& error
) {
    last_status_ = SubmitStatus::Ok;

    if (!submitted_ || !last_event_) {
        return true;
    }

    const double start_s = NowSeconds();

    bool infinite_timeout = timeout_ns == UINT64_MAX;
    double timeout_s =
        infinite_timeout
            ? 0.0
            : static_cast<double>(timeout_ns) / 1000000000.0;

    while (true) {
        cl_int exec_status = 0;

        bool complete = IsEventComplete(last_event_, exec_status, error);
        if (!complete && !error.empty()) {
            return false;
        }

        if (exec_status == CL_COMPLETE) {
            break;
        }

        if (exec_status < 0) {
            SetStatusFromClError(exec_status);
            error = "OpenCL event execution failed: " + ClErrorToString(exec_status);
            return false;
        }

        if (!infinite_timeout && (NowSeconds() - start_s > timeout_s)) {
            last_status_ = SubmitStatus::GpuTimeout;
            error = "OpenCL event wait timeout";
            return false;
        }

        SleepMs(1);
    }

    if (profiling_supported_) {
        cl_ulong start = 0;
        cl_ulong end = 0;

        cl_int err1 = clGetEventProfilingInfo(
            last_event_,
            CL_PROFILING_COMMAND_START,
            sizeof(start),
            &start,
            nullptr
        );

        cl_int err2 = clGetEventProfilingInfo(
            last_event_,
            CL_PROFILING_COMMAND_END,
            sizeof(end),
            &end,
            nullptr
        );

        if (err1 == CL_SUCCESS && err2 == CL_SUCCESS && end >= start) {
            last_gpu_time_ms_ =
                static_cast<double>(end - start) / 1000000.0;
        } else {
            last_gpu_time_ms_ = 0.0;
        }
    }

    clReleaseEvent(last_event_);
    last_event_ = nullptr;
    submitted_ = false;

    return true;
}

bool OpenClBackend::Readback(
    ReadbackBuffer& out,
    std::string& error
) {
    last_status_ = SubmitStatus::Ok;

    if (submitted_) {
        if (!WaitIdleOrFrameDone(UINT64_MAX, error)) {
            return false;
        }
    }

    if (!output_buffer_) {
        error = "output buffer is null";
        last_status_ = SubmitStatus::ApiError;
        return false;
    }

    cl_int err = clEnqueueReadBuffer(
        queue_,
        output_buffer_,
        CL_TRUE,
        0,
        output_size_,
        host_output_.data(),
        0,
        nullptr,
        nullptr
    );

    if (err != CL_SUCCESS) {
        SetStatusFromClError(err);
        error = "clEnqueueReadBuffer failed: " + ClErrorToString(err);
        return false;
    }

    out.width = cfg_.width;
    out.height = cfg_.height;
    out.format = cfg_.rt_format;
    out.data = host_output_;

    return true;
}

bool OpenClBackend::SupportsGpuTimestamp() const {
    return profiling_supported_;
}

bool OpenClBackend::GetLastGpuTimeMs(double& out_ms) {
    if (!profiling_supported_) {
        return false;
    }

    out_ms = last_gpu_time_ms_;
    return true;
}

void OpenClBackend::Destroy() {
    if (last_event_) {
        clReleaseEvent(last_event_);
        last_event_ = nullptr;
    }

    if (kernel_) {
        clReleaseKernel(kernel_);
        kernel_ = nullptr;
    }

    if (program_) {
        clReleaseProgram(program_);
        program_ = nullptr;
    }

    if (output_buffer_) {
        clReleaseMemObject(output_buffer_);
        output_buffer_ = nullptr;
    }

    if (queue_) {
        clReleaseCommandQueue(queue_);
        queue_ = nullptr;
    }

    if (context_) {
        clReleaseContext(context_);
        context_ = nullptr;
    }

    platform_ = nullptr;
    device_ = nullptr;

    host_output_.clear();
    output_size_ = 0;

    submitted_ = false;
    resources_created_ = false;
    profiling_supported_ = true;
    last_gpu_time_ms_ = 0.0;
    last_status_ = SubmitStatus::Ok;
}

} // namespace gpu_avs
