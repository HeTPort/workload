#include "vulkan_backend.h"
#include "vulkan_shader_loader.h"

#include <cstring>
#include <sstream>
#include <vector>

namespace gpu_avs {

VulkanBackend::~VulkanBackend() {
    Destroy();
}

std::string VulkanBackend::VkResultToString(VkResult r) const {
    switch (r) {
        case VK_SUCCESS: return "VK_SUCCESS";
        case VK_NOT_READY: return "VK_NOT_READY";
        case VK_TIMEOUT: return "VK_TIMEOUT";
        case VK_EVENT_SET: return "VK_EVENT_SET";
        case VK_EVENT_RESET: return "VK_EVENT_RESET";
        case VK_INCOMPLETE: return "VK_INCOMPLETE";
        case VK_ERROR_OUT_OF_HOST_MEMORY: return "VK_ERROR_OUT_OF_HOST_MEMORY";
        case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
        case VK_ERROR_INITIALIZATION_FAILED: return "VK_ERROR_INITIALIZATION_FAILED";
        case VK_ERROR_DEVICE_LOST: return "VK_ERROR_DEVICE_LOST";
        case VK_ERROR_MEMORY_MAP_FAILED: return "VK_ERROR_MEMORY_MAP_FAILED";
        case VK_ERROR_LAYER_NOT_PRESENT: return "VK_ERROR_LAYER_NOT_PRESENT";
        case VK_ERROR_EXTENSION_NOT_PRESENT: return "VK_ERROR_EXTENSION_NOT_PRESENT";
        case VK_ERROR_FEATURE_NOT_PRESENT: return "VK_ERROR_FEATURE_NOT_PRESENT";
        case VK_ERROR_INCOMPATIBLE_DRIVER: return "VK_ERROR_INCOMPATIBLE_DRIVER";
        case VK_ERROR_TOO_MANY_OBJECTS: return "VK_ERROR_TOO_MANY_OBJECTS";
        case VK_ERROR_FORMAT_NOT_SUPPORTED: return "VK_ERROR_FORMAT_NOT_SUPPORTED";
        default: return "VK_UNKNOWN_RESULT";
    }
}

void VulkanBackend::SetStatusFromVkResult(VkResult r) {
    if (r == VK_SUCCESS) {
        last_status_ = SubmitStatus::Ok;
    } else if (r == VK_TIMEOUT) {
        last_status_ = SubmitStatus::GpuTimeout;
    } else if (r == VK_ERROR_DEVICE_LOST) {
        last_status_ = SubmitStatus::DeviceLost;
    } else if (r == VK_ERROR_OUT_OF_HOST_MEMORY ||
               r == VK_ERROR_OUT_OF_DEVICE_MEMORY ||
               r == VK_ERROR_MEMORY_MAP_FAILED) {
        last_status_ = SubmitStatus::AllocationFail;
    } else {
        last_status_ = SubmitStatus::ApiError;
    }
}

bool VulkanBackend::Init(const WorkloadConfig& cfg, std::string& error) {
    cfg_ = cfg;
    last_status_ = SubmitStatus::Ok;

    if (cfg_.mode != "offscreen" && cfg_.mode != "compute") {
        error = "Vulkan compute backend supports mode=offscreen or mode=compute";
        last_status_ = SubmitStatus::ApiError;
        return false;
    }

    if (cfg_.rt_format != "RGBA8") {
        error = "Vulkan compute backend supports only rt_format=RGBA8";
        last_status_ = SubmitStatus::ApiError;
        return false;
    }

    if (cfg_.width == 0 || cfg_.height == 0) {
        error = "invalid resolution";
        last_status_ = SubmitStatus::ApiError;
        return false;
    }

    if (!CreateInstance(error)) return false;
    if (!PickPhysicalDevice(error)) return false;
    if (!CreateDevice(error)) return false;

    return true;
}

bool VulkanBackend::CreateResources(std::string& error) {
    last_status_ = SubmitStatus::Ok;

    if (!CreateOutputBuffer(error)) return false;
    if (!CreateDescriptorResources(error)) return false;
    if (!CreatePipeline(error)) return false;
    if (!CreateCommandResources(error)) return false;

    if (!CreateQueryPool(error)) {
        timestamp_supported_ = false;
    }

    resources_created_ = true;
    return true;
}

bool VulkanBackend::CreateInstance(std::string& error) {
    VkApplicationInfo app{};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "gpu-avs-workload";
    app.applicationVersion = VK_MAKE_VERSION(0, 5, 0);
    app.pEngineName = "gpu-avs";
    app.engineVersion = VK_MAKE_VERSION(0, 5, 0);
    app.apiVersion = VK_API_VERSION_1_0;

    VkInstanceCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ci.pApplicationInfo = &app;

    VkResult r = vkCreateInstance(&ci, nullptr, &instance_);
    if (r != VK_SUCCESS) {
        SetStatusFromVkResult(r);
        error = "vkCreateInstance failed: " + VkResultToString(r);
        return false;
    }

    return true;
}

bool VulkanBackend::PickPhysicalDevice(std::string& error) {
    uint32_t count = 0;
    VkResult r = vkEnumeratePhysicalDevices(instance_, &count, nullptr);
    if (r != VK_SUCCESS || count == 0) {
        SetStatusFromVkResult(r);
        error = "vkEnumeratePhysicalDevices failed or no device";
        return false;
    }

    std::vector<VkPhysicalDevice> devices(count);
    r = vkEnumeratePhysicalDevices(instance_, &count, devices.data());
    if (r != VK_SUCCESS) {
        SetStatusFromVkResult(r);
        error = "vkEnumeratePhysicalDevices failed: " + VkResultToString(r);
        return false;
    }

    for (auto dev : devices) {
        uint32_t qcount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(dev, &qcount, nullptr);

        std::vector<VkQueueFamilyProperties> qprops(qcount);
        vkGetPhysicalDeviceQueueFamilyProperties(dev, &qcount, qprops.data());

        for (uint32_t i = 0; i < qcount; ++i) {
            if (qprops[i].queueFlags & VK_QUEUE_COMPUTE_BIT) {
                physical_device_ = dev;
                queue_family_index_ = i;
                vkGetPhysicalDeviceProperties(physical_device_, &device_props_);

                timestamp_supported_ = qprops[i].timestampValidBits > 0;
                timestamp_period_ns_ = device_props_.limits.timestampPeriod;
                return true;
            }
        }
    }

    error = "no Vulkan device with compute queue found";
    last_status_ = SubmitStatus::ApiError;
    return false;
}

bool VulkanBackend::CreateDevice(std::string& error) {
    float priority = 1.0f;

    VkDeviceQueueCreateInfo qci{};
    qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qci.queueFamilyIndex = queue_family_index_;
    qci.queueCount = 1;
    qci.pQueuePriorities = &priority;

    VkDeviceCreateInfo dci{};
    dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;

    VkResult r = vkCreateDevice(physical_device_, &dci, nullptr, &device_);
    if (r != VK_SUCCESS) {
        SetStatusFromVkResult(r);
        error = "vkCreateDevice failed: " + VkResultToString(r);
        return false;
    }

    vkGetDeviceQueue(device_, queue_family_index_, 0, &queue_);
    return true;
}

bool VulkanBackend::FindMemoryType(
    uint32_t type_bits,
    VkMemoryPropertyFlags flags,
    uint32_t& type_index
) const {
    VkPhysicalDeviceMemoryProperties mem_props{};
    vkGetPhysicalDeviceMemoryProperties(physical_device_, &mem_props);

    for (uint32_t i = 0; i < mem_props.memoryTypeCount; ++i) {
        if ((type_bits & (1u << i)) &&
            ((mem_props.memoryTypes[i].propertyFlags & flags) == flags)) {
            type_index = i;
            return true;
        }
    }

    return false;
}

bool VulkanBackend::CreateOutputBuffer(std::string& error) {
    output_size_ =
        static_cast<VkDeviceSize>(cfg_.width) *
        static_cast<VkDeviceSize>(cfg_.height) *
        4u;

    VkBufferCreateInfo bci{};
    bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bci.size = output_size_;
    bci.usage =
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
        VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkResult r = vkCreateBuffer(device_, &bci, nullptr, &output_buffer_);
    if (r != VK_SUCCESS) {
        SetStatusFromVkResult(r);
        error = "vkCreateBuffer failed: " + VkResultToString(r);
        return false;
    }

    VkMemoryRequirements req{};
    vkGetBufferMemoryRequirements(device_, output_buffer_, &req);

    uint32_t mem_type = 0;
    if (!FindMemoryType(
            req.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            mem_type)) {
        error = "failed to find HOST_VISIBLE|HOST_COHERENT memory type";
        last_status_ = SubmitStatus::AllocationFail;
        return false;
    }

    VkMemoryAllocateInfo mai{};
    mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mai.allocationSize = req.size;
    mai.memoryTypeIndex = mem_type;

    r = vkAllocateMemory(device_, &mai, nullptr, &output_memory_);
    if (r != VK_SUCCESS) {
        SetStatusFromVkResult(r);
        error = "vkAllocateMemory failed: " + VkResultToString(r);
        return false;
    }

    r = vkBindBufferMemory(device_, output_buffer_, output_memory_, 0);
    if (r != VK_SUCCESS) {
        SetStatusFromVkResult(r);
        error = "vkBindBufferMemory failed: " + VkResultToString(r);
        return false;
    }

    r = vkMapMemory(device_, output_memory_, 0, output_size_, 0, &output_mapped_);
    if (r != VK_SUCCESS) {
        SetStatusFromVkResult(r);
        error = "vkMapMemory failed: " + VkResultToString(r);
        return false;
    }

    std::memset(output_mapped_, 0, static_cast<size_t>(output_size_));
    return true;
}

bool VulkanBackend::CreateDescriptorResources(std::string& error) {
    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo dlci{};
    dlci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    dlci.bindingCount = 1;
    dlci.pBindings = &binding;

    VkResult r = vkCreateDescriptorSetLayout(
        device_,
        &dlci,
        nullptr,
        &descriptor_set_layout_);

    if (r != VK_SUCCESS) {
        SetStatusFromVkResult(r);
        error = "vkCreateDescriptorSetLayout failed: " + VkResultToString(r);
        return false;
    }

    VkDescriptorPoolSize pool_size{};
    pool_size.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    pool_size.descriptorCount = 1;

    VkDescriptorPoolCreateInfo dpci{};
    dpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    dpci.maxSets = 1;
    dpci.poolSizeCount = 1;
    dpci.pPoolSizes = &pool_size;

    r = vkCreateDescriptorPool(device_, &dpci, nullptr, &descriptor_pool_);
    if (r != VK_SUCCESS) {
        SetStatusFromVkResult(r);
        error = "vkCreateDescriptorPool failed: " + VkResultToString(r);
        return false;
    }

    VkDescriptorSetAllocateInfo dsai{};
    dsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dsai.descriptorPool = descriptor_pool_;
    dsai.descriptorSetCount = 1;
    dsai.pSetLayouts = &descriptor_set_layout_;

    r = vkAllocateDescriptorSets(device_, &dsai, &descriptor_set_);
    if (r != VK_SUCCESS) {
        SetStatusFromVkResult(r);
        error = "vkAllocateDescriptorSets failed: " + VkResultToString(r);
        return false;
    }

    VkDescriptorBufferInfo bi{};
    bi.buffer = output_buffer_;
    bi.offset = 0;
    bi.range = output_size_;

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = descriptor_set_;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    write.pBufferInfo = &bi;

    vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);

    return true;
}

bool VulkanBackend::LoadShaderFile(
    std::vector<uint32_t>& spv,
    std::string& error
) {
    if (!LoadVulkanSpv(cfg_, "workload.comp.spv", spv, error)) {
        last_status_ = SubmitStatus::ApiError;
        return false;
    }

    return true;
}

bool VulkanBackend::CreateShaderModule(
    const std::vector<uint32_t>& spv,
    VkShaderModule& module,
    std::string& error
) {
    VkShaderModuleCreateInfo smci{};
    smci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    smci.codeSize = spv.size() * sizeof(uint32_t);
    smci.pCode = spv.data();

    VkResult r = vkCreateShaderModule(device_, &smci, nullptr, &module);
    if (r != VK_SUCCESS) {
        SetStatusFromVkResult(r);
        error = "vkCreateShaderModule failed: " + VkResultToString(r);
        return false;
    }

    return true;
}

bool VulkanBackend::CreatePipeline(std::string& error) {
    std::vector<uint32_t> spv;
    if (!LoadShaderFile(spv, error)) {
        return false;
    }

    VkShaderModule module = VK_NULL_HANDLE;
    if (!CreateShaderModule(spv, module, error)) {
        return false;
    }

    VkPushConstantRange pc{};
    pc.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pc.offset = 0;
    pc.size = sizeof(PushConstants);

    VkPipelineLayoutCreateInfo plci{};
    plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    plci.setLayoutCount = 1;
    plci.pSetLayouts = &descriptor_set_layout_;
    plci.pushConstantRangeCount = 1;
    plci.pPushConstantRanges = &pc;

    VkResult r = vkCreatePipelineLayout(
        device_,
        &plci,
        nullptr,
        &pipeline_layout_);

    if (r != VK_SUCCESS) {
        vkDestroyShaderModule(device_, module, nullptr);
        SetStatusFromVkResult(r);
        error = "vkCreatePipelineLayout failed: " + VkResultToString(r);
        return false;
    }

    VkPipelineShaderStageCreateInfo stage{};
    stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stage.module = module;
    stage.pName = "main";

    VkComputePipelineCreateInfo cpci{};
    cpci.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    cpci.stage = stage;
    cpci.layout = pipeline_layout_;

    r = vkCreateComputePipelines(
        device_,
        VK_NULL_HANDLE,
        1,
        &cpci,
        nullptr,
        &pipeline_);

    vkDestroyShaderModule(device_, module, nullptr);

    if (r != VK_SUCCESS) {
        SetStatusFromVkResult(r);
        error = "vkCreateComputePipelines failed: " + VkResultToString(r);
        return false;
    }

    return true;
}

bool VulkanBackend::CreateCommandResources(std::string& error) {
    VkCommandPoolCreateInfo cpci{};
    cpci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    cpci.queueFamilyIndex = queue_family_index_;
    cpci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    VkResult r = vkCreateCommandPool(device_, &cpci, nullptr, &command_pool_);
    if (r != VK_SUCCESS) {
        SetStatusFromVkResult(r);
        error = "vkCreateCommandPool failed: " + VkResultToString(r);
        return false;
    }

    VkCommandBufferAllocateInfo cbai{};
    cbai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cbai.commandPool = command_pool_;
    cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbai.commandBufferCount = 1;

    r = vkAllocateCommandBuffers(device_, &cbai, &command_buffer_);
    if (r != VK_SUCCESS) {
        SetStatusFromVkResult(r);
        error = "vkAllocateCommandBuffers failed: " + VkResultToString(r);
        return false;
    }

    VkFenceCreateInfo fci{};
    fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;

    r = vkCreateFence(device_, &fci, nullptr, &fence_);
    if (r != VK_SUCCESS) {
        SetStatusFromVkResult(r);
        error = "vkCreateFence failed: " + VkResultToString(r);
        return false;
    }

    return true;
}

bool VulkanBackend::CreateQueryPool(std::string& error) {
    if (!timestamp_supported_) {
        error = "timestamp not supported by selected queue family";
        return false;
    }

    VkQueryPoolCreateInfo qpci{};
    qpci.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    qpci.queryType = VK_QUERY_TYPE_TIMESTAMP;
    qpci.queryCount = 2;

    VkResult r = vkCreateQueryPool(device_, &qpci, nullptr, &query_pool_);
    if (r != VK_SUCCESS) {
        query_pool_ = VK_NULL_HANDLE;
        timestamp_supported_ = false;
        error = "vkCreateQueryPool failed: " + VkResultToString(r);
        return false;
    }

    return true;
}

uint32_t VulkanBackend::ShaderId() const {
    if (cfg_.shader == "alu") return 1;
    if (cfg_.shader == "sfu") return 2;
    if (cfg_.shader == "texture") return 3;
    if (cfg_.shader == "fill") return 4;
    return 5;
}

SubmitStatus VulkanBackend::SubmitWorkload(uint64_t frame_index) {
    last_status_ = SubmitStatus::Ok;

    if (!resources_created_) {
        last_status_ = SubmitStatus::AllocationFail;
        return last_status_;
    }

    if (submitted_) {
        VkResult wr = vkWaitForFences(device_, 1, &fence_, VK_TRUE, UINT64_MAX);
        SetStatusFromVkResult(wr);
        if (wr != VK_SUCCESS) {
            return last_status_;
        }
        submitted_ = false;
    }

    VkResult r = vkResetFences(device_, 1, &fence_);
    if (r != VK_SUCCESS) {
        SetStatusFromVkResult(r);
        return last_status_;
    }

    r = vkResetCommandBuffer(command_buffer_, 0);
    if (r != VK_SUCCESS) {
        SetStatusFromVkResult(r);
        return last_status_;
    }

    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    r = vkBeginCommandBuffer(command_buffer_, &bi);
    if (r != VK_SUCCESS) {
        SetStatusFromVkResult(r);
        return last_status_;
    }

    if (timestamp_supported_ && query_pool_ != VK_NULL_HANDLE) {
        vkCmdResetQueryPool(command_buffer_, query_pool_, 0, 2);
        vkCmdWriteTimestamp(
            command_buffer_,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            query_pool_,
            0
        );
    }

    vkCmdBindPipeline(
        command_buffer_,
        VK_PIPELINE_BIND_POINT_COMPUTE,
        pipeline_
    );

    vkCmdBindDescriptorSets(
        command_buffer_,
        VK_PIPELINE_BIND_POINT_COMPUTE,
        pipeline_layout_,
        0,
        1,
        &descriptor_set_,
        0,
        nullptr
    );

    PushConstants pc{};
    pc.width = cfg_.width;
    pc.height = cfg_.height;
    pc.iterations = cfg_.iterations;
    pc.shader_id = ShaderId();
    pc.texture_count = cfg_.texture_count;
    pc.frame_index = static_cast<uint32_t>(frame_index);

    vkCmdPushConstants(
        command_buffer_,
        pipeline_layout_,
        VK_SHADER_STAGE_COMPUTE_BIT,
        0,
        sizeof(PushConstants),
        &pc
    );

    const uint32_t group_x = (cfg_.width + 15u) / 16u;
    const uint32_t group_y = (cfg_.height + 15u) / 16u;

    vkCmdDispatch(command_buffer_, group_x, group_y, 1);

    VkBufferMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.buffer = output_buffer_;
    barrier.offset = 0;
    barrier.size = output_size_;

    vkCmdPipelineBarrier(
        command_buffer_,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_HOST_BIT,
        0,
        0,
        nullptr,
        1,
        &barrier,
        0,
        nullptr
    );

    if (timestamp_supported_ && query_pool_ != VK_NULL_HANDLE) {
        vkCmdWriteTimestamp(
            command_buffer_,
            VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
            query_pool_,
            1
        );
    }

    r = vkEndCommandBuffer(command_buffer_);
    if (r != VK_SUCCESS) {
        SetStatusFromVkResult(r);
        return last_status_;
    }

    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &command_buffer_;

    r = vkQueueSubmit(queue_, 1, &si, fence_);
    if (r != VK_SUCCESS) {
        SetStatusFromVkResult(r);
        return last_status_;
    }

    submitted_ = true;
    return SubmitStatus::Ok;
}

bool VulkanBackend::WaitIdleOrFrameDone(uint64_t timeout_ns, std::string& error) {
    last_status_ = SubmitStatus::Ok;

    if (!submitted_) {
        return true;
    }

    VkResult r = vkWaitForFences(
        device_,
        1,
        &fence_,
        VK_TRUE,
        timeout_ns
    );

    if (r != VK_SUCCESS) {
        SetStatusFromVkResult(r);
        error = "vkWaitForFences failed: " + VkResultToString(r);
        return false;
    }

    submitted_ = false;

    if (timestamp_supported_ && query_pool_ != VK_NULL_HANDLE) {
        uint64_t timestamps[2] = {};

        r = vkGetQueryPoolResults(
            device_,
            query_pool_,
            0,
            2,
            sizeof(timestamps),
            timestamps,
            sizeof(uint64_t),
            VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT
        );

        if (r == VK_SUCCESS && timestamps[1] >= timestamps[0]) {
            double elapsed_ticks =
                static_cast<double>(timestamps[1] - timestamps[0]);

            last_gpu_time_ms_ =
                elapsed_ticks * timestamp_period_ns_ / 1000000.0;
        } else {
            last_gpu_time_ms_ = 0.0;
        }
    }

    return true;
}

bool VulkanBackend::Readback(ReadbackBuffer& out, std::string& error) {
    last_status_ = SubmitStatus::Ok;

    if (submitted_) {
        if (!WaitIdleOrFrameDone(UINT64_MAX, error)) {
            return false;
        }
    }

    if (!output_mapped_) {
        error = "output buffer is not mapped";
        last_status_ = SubmitStatus::ApiError;
        return false;
    }

    out.width = cfg_.width;
    out.height = cfg_.height;
    out.format = cfg_.rt_format;
    out.data.resize(static_cast<size_t>(output_size_));

    std::memcpy(
        out.data.data(),
        output_mapped_,
        static_cast<size_t>(output_size_)
    );

    return true;
}

bool VulkanBackend::SupportsGpuTimestamp() const {
    return timestamp_supported_ && query_pool_ != VK_NULL_HANDLE;
}

bool VulkanBackend::GetLastGpuTimeMs(double& out_ms) {
    if (!SupportsGpuTimestamp()) {
        return false;
    }

    out_ms = last_gpu_time_ms_;
    return true;
}

void VulkanBackend::CleanupVulkanObjects() {
    if (device_ != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device_);
    }

    if (output_mapped_ && output_memory_ != VK_NULL_HANDLE) {
        vkUnmapMemory(device_, output_memory_);
        output_mapped_ = nullptr;
    }

    if (query_pool_ != VK_NULL_HANDLE) {
        vkDestroyQueryPool(device_, query_pool_, nullptr);
        query_pool_ = VK_NULL_HANDLE;
    }

    if (fence_ != VK_NULL_HANDLE) {
        vkDestroyFence(device_, fence_, nullptr);
        fence_ = VK_NULL_HANDLE;
    }

    if (command_pool_ != VK_NULL_HANDLE) {
        vkDestroyCommandPool(device_, command_pool_, nullptr);
        command_pool_ = VK_NULL_HANDLE;
        command_buffer_ = VK_NULL_HANDLE;
    }

    if (pipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(device_, pipeline_, nullptr);
        pipeline_ = VK_NULL_HANDLE;
    }

    if (pipeline_layout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device_, pipeline_layout_, nullptr);
        pipeline_layout_ = VK_NULL_HANDLE;
    }

    if (descriptor_pool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device_, descriptor_pool_, nullptr);
        descriptor_pool_ = VK_NULL_HANDLE;
    }

    if (descriptor_set_layout_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device_, descriptor_set_layout_, nullptr);
        descriptor_set_layout_ = VK_NULL_HANDLE;
    }

    if (output_buffer_ != VK_NULL_HANDLE) {
        vkDestroyBuffer(device_, output_buffer_, nullptr);
        output_buffer_ = VK_NULL_HANDLE;
    }

    if (output_memory_ != VK_NULL_HANDLE) {
        vkFreeMemory(device_, output_memory_, nullptr);
        output_memory_ = VK_NULL_HANDLE;
    }

    if (device_ != VK_NULL_HANDLE) {
        vkDestroyDevice(device_, nullptr);
        device_ = VK_NULL_HANDLE;
    }

    if (instance_ != VK_NULL_HANDLE) {
        vkDestroyInstance(instance_, nullptr);
        instance_ = VK_NULL_HANDLE;
    }
}

void VulkanBackend::Destroy() {
    CleanupVulkanObjects();

    physical_device_ = VK_NULL_HANDLE;
    queue_ = VK_NULL_HANDLE;
    output_size_ = 0;
    timestamp_supported_ = false;
    last_gpu_time_ms_ = 0.0;
    submitted_ = false;
    resources_created_ = false;
    last_status_ = SubmitStatus::Ok;
}

} // namespace gpu_avs
