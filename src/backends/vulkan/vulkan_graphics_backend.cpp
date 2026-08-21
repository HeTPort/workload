#include "vulkan_graphics_backend.h"
#include "vulkan_shader_loader.h"

#include <algorithm>
#include <cstring>
#include <sstream>
#include <vector>

namespace gpu_avs {

VulkanGraphicsBackend::~VulkanGraphicsBackend() {
    Destroy();
}

std::string VulkanGraphicsBackend::VkResultToString(VkResult r) const {
    switch (r) {
        case VK_SUCCESS: return "VK_SUCCESS";
        case VK_NOT_READY: return "VK_NOT_READY";
        case VK_TIMEOUT: return "VK_TIMEOUT";
        case VK_ERROR_OUT_OF_HOST_MEMORY: return "VK_ERROR_OUT_OF_HOST_MEMORY";
        case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
        case VK_ERROR_INITIALIZATION_FAILED: return "VK_ERROR_INITIALIZATION_FAILED";
        case VK_ERROR_DEVICE_LOST: return "VK_ERROR_DEVICE_LOST";
        case VK_ERROR_MEMORY_MAP_FAILED: return "VK_ERROR_MEMORY_MAP_FAILED";
        case VK_ERROR_LAYER_NOT_PRESENT: return "VK_ERROR_LAYER_NOT_PRESENT";
        case VK_ERROR_EXTENSION_NOT_PRESENT: return "VK_ERROR_EXTENSION_NOT_PRESENT";
        case VK_ERROR_FEATURE_NOT_PRESENT: return "VK_ERROR_FEATURE_NOT_PRESENT";
        case VK_ERROR_INCOMPATIBLE_DRIVER: return "VK_ERROR_INCOMPATIBLE_DRIVER";
        case VK_ERROR_FORMAT_NOT_SUPPORTED: return "VK_ERROR_FORMAT_NOT_SUPPORTED";
        default: return "VK_UNKNOWN_RESULT";
    }
}

void VulkanGraphicsBackend::SetStatusFromVkResult(VkResult r) {
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

bool VulkanGraphicsBackend::Init(const WorkloadConfig& cfg, std::string& error) {
    cfg_ = cfg;
    last_status_ = SubmitStatus::Ok;

    if (cfg_.mode != "offscreen") {
        error = "VulkanGraphicsBackend supports only mode=offscreen";
        last_status_ = SubmitStatus::ApiError;
        return false;
    }

    if (cfg_.rt_format != "RGBA8") {
        error = "VulkanGraphicsBackend supports only rt_format=RGBA8";
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

bool VulkanGraphicsBackend::CreateResources(std::string& error) {
    if (!CreateCommandResources(error)) return false;
    if (!CreateColorTarget(error)) return false;
    if (!CreateReadbackBuffer(error)) return false;
    if (!CreateTexture(error)) return false;
    if (!CreateDescriptorResources(error)) return false;
    if (!CreateRenderPass(error)) return false;
    if (!CreateFramebuffer(error)) return false;
    if (!CreatePipeline(error)) return false;

    if (!CreateQueryPool(error)) {
        timestamp_supported_ = false;
    }

    resources_created_ = true;
    return true;
}

bool VulkanGraphicsBackend::CreateInstance(std::string& error) {
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

bool VulkanGraphicsBackend::PickPhysicalDevice(std::string& error) {
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
            if (qprops[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                physical_device_ = dev;
                queue_family_index_ = i;

                vkGetPhysicalDeviceProperties(physical_device_, &device_props_);

                timestamp_supported_ = qprops[i].timestampValidBits > 0;
                timestamp_period_ns_ = device_props_.limits.timestampPeriod;

                return true;
            }
        }
    }

    error = "no Vulkan graphics queue found";
    last_status_ = SubmitStatus::ApiError;
    return false;
}

bool VulkanGraphicsBackend::CreateDevice(std::string& error) {
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

bool VulkanGraphicsBackend::FindMemoryType(
    uint32_t type_bits,
    VkMemoryPropertyFlags flags,
    uint32_t& type_index
) const {
    VkPhysicalDeviceMemoryProperties props{};
    vkGetPhysicalDeviceMemoryProperties(physical_device_, &props);

    for (uint32_t i = 0; i < props.memoryTypeCount; ++i) {
        if ((type_bits & (1u << i)) &&
            ((props.memoryTypes[i].propertyFlags & flags) == flags)) {
            type_index = i;
            return true;
        }
    }

    return false;
}

bool VulkanGraphicsBackend::CreateBuffer(
    VkDeviceSize size,
    VkBufferUsageFlags usage,
    VkMemoryPropertyFlags mem_flags,
    VkBuffer& buffer,
    VkDeviceMemory& memory,
    void** mapped,
    std::string& error
) {
    VkBufferCreateInfo bci{};
    bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bci.size = size;
    bci.usage = usage;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkResult r = vkCreateBuffer(device_, &bci, nullptr, &buffer);
    if (r != VK_SUCCESS) {
        SetStatusFromVkResult(r);
        error = "vkCreateBuffer failed: " + VkResultToString(r);
        return false;
    }

    VkMemoryRequirements req{};
    vkGetBufferMemoryRequirements(device_, buffer, &req);

    uint32_t mem_type = 0;
    if (!FindMemoryType(req.memoryTypeBits, mem_flags, mem_type)) {
        error = "failed to find memory type for buffer";
        last_status_ = SubmitStatus::AllocationFail;
        return false;
    }

    VkMemoryAllocateInfo mai{};
    mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mai.allocationSize = req.size;
    mai.memoryTypeIndex = mem_type;

    r = vkAllocateMemory(device_, &mai, nullptr, &memory);
    if (r != VK_SUCCESS) {
        SetStatusFromVkResult(r);
        error = "vkAllocateMemory buffer failed: " + VkResultToString(r);
        return false;
    }

    r = vkBindBufferMemory(device_, buffer, memory, 0);
    if (r != VK_SUCCESS) {
        SetStatusFromVkResult(r);
        error = "vkBindBufferMemory failed: " + VkResultToString(r);
        return false;
    }

    if (mapped) {
        r = vkMapMemory(device_, memory, 0, size, 0, mapped);
        if (r != VK_SUCCESS) {
            SetStatusFromVkResult(r);
            error = "vkMapMemory failed: " + VkResultToString(r);
            return false;
        }
    }

    return true;
}

bool VulkanGraphicsBackend::CreateImage(
    uint32_t width,
    uint32_t height,
    VkFormat format,
    VkImageUsageFlags usage,
    VkImage& image,
    VkDeviceMemory& memory,
    std::string& error
) {
    VkImageCreateInfo ici{};
    ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.extent.width = width;
    ici.extent.height = height;
    ici.extent.depth = 1;
    ici.mipLevels = 1;
    ici.arrayLayers = 1;
    ici.format = format;
    ici.tiling = VK_IMAGE_TILING_OPTIMAL;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    ici.usage = usage;
    ici.samples = VK_SAMPLE_COUNT_1_BIT;
    ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkResult r = vkCreateImage(device_, &ici, nullptr, &image);
    if (r != VK_SUCCESS) {
        SetStatusFromVkResult(r);
        error = "vkCreateImage failed: " + VkResultToString(r);
        return false;
    }

    VkMemoryRequirements req{};
    vkGetImageMemoryRequirements(device_, image, &req);

    uint32_t mem_type = 0;
    if (!FindMemoryType(
            req.memoryTypeBits,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            mem_type)) {
        error = "failed to find device local image memory";
        last_status_ = SubmitStatus::AllocationFail;
        return false;
    }

    VkMemoryAllocateInfo mai{};
    mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    mai.allocationSize = req.size;
    mai.memoryTypeIndex = mem_type;

    r = vkAllocateMemory(device_, &mai, nullptr, &memory);
    if (r != VK_SUCCESS) {
        SetStatusFromVkResult(r);
        error = "vkAllocateMemory image failed: " + VkResultToString(r);
        return false;
    }

    r = vkBindImageMemory(device_, image, memory, 0);
    if (r != VK_SUCCESS) {
        SetStatusFromVkResult(r);
        error = "vkBindImageMemory failed: " + VkResultToString(r);
        return false;
    }

    return true;
}

bool VulkanGraphicsBackend::CreateImageView(
    VkImage image,
    VkFormat format,
    VkImageView& view,
    std::string& error
) {
    VkImageViewCreateInfo ivci{};
    ivci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    ivci.image = image;
    ivci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    ivci.format = format;
    ivci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    ivci.subresourceRange.baseMipLevel = 0;
    ivci.subresourceRange.levelCount = 1;
    ivci.subresourceRange.baseArrayLayer = 0;
    ivci.subresourceRange.layerCount = 1;

    VkResult r = vkCreateImageView(device_, &ivci, nullptr, &view);
    if (r != VK_SUCCESS) {
        SetStatusFromVkResult(r);
        error = "vkCreateImageView failed: " + VkResultToString(r);
        return false;
    }

    return true;
}

bool VulkanGraphicsBackend::CreateCommandResources(std::string& error) {
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

bool VulkanGraphicsBackend::CreateColorTarget(std::string& error) {
    if (!CreateImage(
            cfg_.width,
            cfg_.height,
            color_format_,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
            VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
            color_image_,
            color_memory_,
            error)) {
        return false;
    }

    return CreateImageView(color_image_, color_format_, color_view_, error);
}

bool VulkanGraphicsBackend::CreateReadbackBuffer(std::string& error) {
    readback_size_ =
        static_cast<VkDeviceSize>(cfg_.width) *
        static_cast<VkDeviceSize>(cfg_.height) *
        4u;

    return CreateBuffer(
        readback_size_,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        readback_buffer_,
        readback_memory_,
        &readback_mapped_,
        error
    );
}

bool VulkanGraphicsBackend::BeginOneTimeCommands(
    VkCommandBuffer& cmd,
    std::string& error
) {
    VkCommandBufferAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool = command_pool_;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;

    VkResult r = vkAllocateCommandBuffers(device_, &ai, &cmd);
    if (r != VK_SUCCESS) {
        SetStatusFromVkResult(r);
        error = "vkAllocateCommandBuffers one-time failed: " + VkResultToString(r);
        return false;
    }

    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    r = vkBeginCommandBuffer(cmd, &bi);
    if (r != VK_SUCCESS) {
        SetStatusFromVkResult(r);
        error = "vkBeginCommandBuffer one-time failed: " + VkResultToString(r);
        return false;
    }

    return true;
}

bool VulkanGraphicsBackend::EndOneTimeCommands(
    VkCommandBuffer cmd,
    std::string& error
) {
    VkResult r = vkEndCommandBuffer(cmd);
    if (r != VK_SUCCESS) {
        SetStatusFromVkResult(r);
        error = "vkEndCommandBuffer one-time failed: " + VkResultToString(r);
        return false;
    }

    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;

    r = vkQueueSubmit(queue_, 1, &si, VK_NULL_HANDLE);
    if (r != VK_SUCCESS) {
        SetStatusFromVkResult(r);
        error = "vkQueueSubmit one-time failed: " + VkResultToString(r);
        return false;
    }

    r = vkQueueWaitIdle(queue_);
    if (r != VK_SUCCESS) {
        SetStatusFromVkResult(r);
        error = "vkQueueWaitIdle one-time failed: " + VkResultToString(r);
        return false;
    }

    vkFreeCommandBuffers(device_, command_pool_, 1, &cmd);
    return true;
}

void VulkanGraphicsBackend::CmdTransitionImage(
    VkCommandBuffer cmd,
    VkImage image,
    VkImageLayout old_layout,
    VkImageLayout new_layout,
    VkAccessFlags src_access,
    VkAccessFlags dst_access,
    VkPipelineStageFlags src_stage,
    VkPipelineStageFlags dst_stage
) {
    VkImageMemoryBarrier b{};
    b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    b.oldLayout = old_layout;
    b.newLayout = new_layout;
    b.srcAccessMask = src_access;
    b.dstAccessMask = dst_access;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = image;
    b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    b.subresourceRange.baseMipLevel = 0;
    b.subresourceRange.levelCount = 1;
    b.subresourceRange.baseArrayLayer = 0;
    b.subresourceRange.layerCount = 1;

    vkCmdPipelineBarrier(
        cmd,
        src_stage,
        dst_stage,
        0,
        0,
        nullptr,
        0,
        nullptr,
        1,
        &b
    );
}

uint32_t VulkanGraphicsBackend::ParseTextureWidth() const {
    size_t pos = cfg_.texture_size.find('x');
    if (pos == std::string::npos) pos = cfg_.texture_size.find('X');
    if (pos == std::string::npos) return 512;

    try {
        return std::max(
            1u,
            std::min(
                8192u,
                static_cast<uint32_t>(std::stoul(cfg_.texture_size.substr(0, pos)))
            )
        );
    } catch (...) {
        return 512;
    }
}

uint32_t VulkanGraphicsBackend::ParseTextureHeight() const {
    size_t pos = cfg_.texture_size.find('x');
    if (pos == std::string::npos) pos = cfg_.texture_size.find('X');
    if (pos == std::string::npos) return 512;

    try {
        return std::max(
            1u,
            std::min(
                8192u,
                static_cast<uint32_t>(std::stoul(cfg_.texture_size.substr(pos + 1)))
            )
        );
    } catch (...) {
        return 512;
    }
}

static uint32_t HashU32Local(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

bool VulkanGraphicsBackend::CreateTexture(std::string& error) {
    uint32_t tex_w = ParseTextureWidth();
    uint32_t tex_h = ParseTextureHeight();

    VkDeviceSize tex_size =
        static_cast<VkDeviceSize>(tex_w) *
        static_cast<VkDeviceSize>(tex_h) *
        4u;

    VkBuffer staging = VK_NULL_HANDLE;
    VkDeviceMemory staging_mem = VK_NULL_HANDLE;
    void* staging_mapped = nullptr;

    if (!CreateBuffer(
            tex_size,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            staging,
            staging_mem,
            &staging_mapped,
            error)) {
        return false;
    }

    auto* bytes = reinterpret_cast<uint8_t*>(staging_mapped);
    for (uint32_t y = 0; y < tex_h; ++y) {
        for (uint32_t x = 0; x < tex_w; ++x) {
            uint32_t h = HashU32Local(
                x * 73856093u ^
                y * 19349663u ^
                cfg_.iterations * 83492791u ^
                cfg_.texture_count * 2654435761u
            );

            size_t idx = (static_cast<size_t>(y) * tex_w + x) * 4;
            bytes[idx + 0] = static_cast<uint8_t>((h >> 0) & 0xff);
            bytes[idx + 1] = static_cast<uint8_t>((h >> 8) & 0xff);
            bytes[idx + 2] = static_cast<uint8_t>((h >> 16) & 0xff);
            bytes[idx + 3] = 255;
        }
    }

    if (!CreateImage(
            tex_w,
            tex_h,
            color_format_,
            VK_IMAGE_USAGE_SAMPLED_BIT |
            VK_IMAGE_USAGE_TRANSFER_DST_BIT,
            texture_image_,
            texture_memory_,
            error)) {
        vkUnmapMemory(device_, staging_mem);
        vkDestroyBuffer(device_, staging, nullptr);
        vkFreeMemory(device_, staging_mem, nullptr);
        return false;
    }

    VkCommandBuffer cmd = VK_NULL_HANDLE;
    if (!BeginOneTimeCommands(cmd, error)) {
        vkUnmapMemory(device_, staging_mem);
        vkDestroyBuffer(device_, staging, nullptr);
        vkFreeMemory(device_, staging_mem, nullptr);
        return false;
    }

    CmdTransitionImage(
        cmd,
        texture_image_,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        0,
        VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT
    );

    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {tex_w, tex_h, 1};

    vkCmdCopyBufferToImage(
        cmd,
        staging,
        texture_image_,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1,
        &region
    );

    CmdTransitionImage(
        cmd,
        texture_image_,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_ACCESS_SHADER_READ_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
    );

    bool ok = EndOneTimeCommands(cmd, error);

    vkUnmapMemory(device_, staging_mem);
    vkDestroyBuffer(device_, staging, nullptr);
    vkFreeMemory(device_, staging_mem, nullptr);

    if (!ok) {
        return false;
    }

    if (!CreateImageView(texture_image_, color_format_, texture_view_, error)) {
        return false;
    }

    VkSamplerCreateInfo sci{};
    sci.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sci.magFilter = VK_FILTER_LINEAR;
    sci.minFilter = VK_FILTER_LINEAR;
    sci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sci.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sci.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sci.maxLod = 1.0f;

    VkResult r = vkCreateSampler(device_, &sci, nullptr, &sampler_);
    if (r != VK_SUCCESS) {
        SetStatusFromVkResult(r);
        error = "vkCreateSampler failed: " + VkResultToString(r);
        return false;
    }

    return true;
}

bool VulkanGraphicsBackend::CreateDescriptorResources(std::string& error) {
    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo lci{};
    lci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    lci.bindingCount = 1;
    lci.pBindings = &binding;

    VkResult r = vkCreateDescriptorSetLayout(
        device_,
        &lci,
        nullptr,
        &descriptor_set_layout_);

    if (r != VK_SUCCESS) {
        SetStatusFromVkResult(r);
        error = "vkCreateDescriptorSetLayout failed: " + VkResultToString(r);
        return false;
    }

    VkDescriptorPoolSize ps{};
    ps.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    ps.descriptorCount = 1;

    VkDescriptorPoolCreateInfo pci{};
    pci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pci.maxSets = 1;
    pci.poolSizeCount = 1;
    pci.pPoolSizes = &ps;

    r = vkCreateDescriptorPool(device_, &pci, nullptr, &descriptor_pool_);
    if (r != VK_SUCCESS) {
        SetStatusFromVkResult(r);
        error = "vkCreateDescriptorPool failed: " + VkResultToString(r);
        return false;
    }

    VkDescriptorSetAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool = descriptor_pool_;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts = &descriptor_set_layout_;

    r = vkAllocateDescriptorSets(device_, &ai, &descriptor_set_);
    if (r != VK_SUCCESS) {
        SetStatusFromVkResult(r);
        error = "vkAllocateDescriptorSets failed: " + VkResultToString(r);
        return false;
    }

    VkDescriptorImageInfo ii{};
    ii.sampler = sampler_;
    ii.imageView = texture_view_;
    ii.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = descriptor_set_;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &ii；

    vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);
    return true;
}

bool VulkanGraphicsBackend::CreateRenderPass(std::string& error) {
    VkAttachmentDescription color{};
    color.format = color_format_;
    color.samples = VK_SAMPLE_COUNT_1_BIT;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference ref{};
    ref.attachment = 0;
    ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &ref;

    VkRenderPassCreateInfo rpci{};
    rpci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rpci.attachmentCount = 1;
    rpci.pAttachments = &color;
    rpci.subpassCount = 1;
    rpci.pSubpasses = &subpass;

    VkResult r = vkCreateRenderPass(device_, &rpci, nullptr, &render_pass_);
    if (r != VK_SUCCESS) {
        SetStatusFromVkResult(r);
        error = "vkCreateRenderPass failed: " + VkResultToString(r);
        return false;
    }

    return true;
}

bool VulkanGraphicsBackend::CreateFramebuffer(std::string& error) {
    VkFramebufferCreateInfo fci{};
    fci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fci.renderPass = render_pass_;
    fci.attachmentCount = 1;
    fci.pAttachments = &color_view_;
    fci.width = cfg_.width;
    fci.height = cfg_.height;
    fci.layers = 1;

    VkResult r = vkCreateFramebuffer(device_, &fci, nullptr, &framebuffer_);
    if (r != VK_SUCCESS) {
        SetStatusFromVkResult(r);
        error = "vkCreateFramebuffer failed: " + VkResultToString(r);
        return false;
    }

    return true;
}

bool VulkanGraphicsBackend::LoadShaderFile(
    const std::string& name,
    std::vector<uint32_t>& spv,
    std::string& error
) {
    if (!LoadVulkanSpv(cfg_, name, spv, error)) {
        last_status_ = SubmitStatus::ApiError;
        return false;
    }

    return true;
}

bool VulkanGraphicsBackend::CreateShaderModule(
    const std::vector<uint32_t>& spv,
    VkShaderModule& module,
    std::string& error
) {
    VkShaderModuleCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.codeSize = spv.size() * sizeof(uint32_t);
    ci.pCode = spv.data();

    VkResult r = vkCreateShaderModule(device_, &ci, nullptr, &module);
    if (r != VK_SUCCESS) {
        SetStatusFromVkResult(r);
        error = "vkCreateShaderModule failed: " + VkResultToString(r);
        return false;
    }

    return true;
}

bool VulkanGraphicsBackend::CreatePipeline(std::string& error) {
    std::vector<uint32_t> vert_spv;
    std::vector<uint32_t> frag_spv;

    if (!LoadShaderFile("fullscreen.vert.spv", vert_spv, error)) return false;
    if (!LoadShaderFile("workload.frag.spv", frag_spv, error)) return false;

    VkShaderModule vert = VK_NULL_HANDLE;
    VkShaderModule frag = VK_NULL_HANDLE;

    if (!CreateShaderModule(vert_spv, vert, error)) return false;
    if (!CreateShaderModule(frag_spv, frag, error)) {
        vkDestroyShaderModule(device_, vert, nullptr);
        return false;
    }

    VkPipelineShaderStageCreateInfo stages[2]{};

    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert;
    stages[0].pName = "main";

    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag;
    stages[1].pName = "main";

    VkPipelineVertexInputStateCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkPipelineInputAssemblyStateCreateInfo ia{};
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(cfg_.width);
    viewport.height = static_cast<float>(cfg_.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = {cfg_.width, cfg_.height};

    VkPipelineViewportStateCreateInfo vp{};
    vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp.viewportCount = 1;
    vp.pViewports = &viewport;
    vp.scissorCount = 1;
    vp.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo rs{};
    rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState cb_att{};
    cb_att.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT |
        VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT |
        VK_COLOR_COMPONENT_A_BIT;
    cb_att.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo cb{};
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = 1;
    cb.pAttachments = &cb_att;

    VkPushConstantRange pc{};
    pc.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
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
        vkDestroyShaderModule(device_, vert, nullptr);
        vkDestroyShaderModule(device_, frag, nullptr);
        SetStatusFromVkResult(r);
        error = "vkCreatePipelineLayout failed: " + VkResultToString(r);
        return false;
    }

    VkGraphicsPipelineCreateInfo gpci{};
    gpci.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    gpci.stageCount = 2;
    gpci.pStages = stages;
    gpci.pVertexInputState = &vi;
    gpci.pInputAssemblyState = &ia;
    gpci.pViewportState = &vp;
    gpci.pRasterizationState = &rs;
    gpci.pMultisampleState = &ms;
    gpci.pColorBlendState = &cb;
    gpci.layout = pipeline_layout_;
    gpci.renderPass = render_pass_;
    gpci.subpass = 0;

    r = vkCreateGraphicsPipelines(
        device_,
        VK_NULL_HANDLE,
        1,
        &gpci,
        nullptr,
        &pipeline_);

    vkDestroyShaderModule(device_, vert, nullptr);
    vkDestroyShaderModule(device_, frag, nullptr);

    if (r != VK_SUCCESS) {
        SetStatusFromVkResult(r);
        error = "vkCreateGraphicsPipelines failed: " + VkResultToString(r);
        return false;
    }

    return true;
}

bool VulkanGraphicsBackend::CreateQueryPool(std::string& error) {
    if (!timestamp_supported_) {
        error = "timestamp not supported";
        return false;
    }

    VkQueryPoolCreateInfo qci{};
    qci.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    qci.queryType = VK_QUERY_TYPE_TIMESTAMP;
    qci.queryCount = 2;

    VkResult r = vkCreateQueryPool(device_, &qci, nullptr, &query_pool_);
    if (r != VK_SUCCESS) {
        SetStatusFromVkResult(r);
        timestamp_supported_ = false;
        query_pool_ = VK_NULL_HANDLE;
        error = "vkCreateQueryPool failed: " + VkResultToString(r);
        return false;
    }

    return true;
}

uint32_t VulkanGraphicsBackend::ShaderId() const {
    if (cfg_.shader == "alu") return 1;
    if (cfg_.shader == "sfu") return 2;
    if (cfg_.shader == "texture") return 3;
    if (cfg_.shader == "fill") return 4;
    return 5;
}

SubmitStatus VulkanGraphicsBackend::SubmitWorkload(uint64_t frame_index) {
    last_status_ = SubmitStatus::Ok;

    if (!resources_created_) {
        last_status_ = SubmitStatus::AllocationFail;
        return last_status_;
    }

    if (submitted_) {
        VkResult wr = vkWaitForFences(device_, 1, &fence_, VK_TRUE, UINT64_MAX);
        SetStatusFromVkResult(wr);
        if (wr != VK_SUCCESS) return last_status_;
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

    CmdTransitionImage(
        command_buffer_,
        color_image_,
        color_layout_,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        color_layout_ == VK_IMAGE_LAYOUT_UNDEFINED ? 0 : VK_ACCESS_TRANSFER_READ_BIT,
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        color_layout_ == VK_IMAGE_LAYOUT_UNDEFINED
            ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT
            : VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
    );

    color_layout_ = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkClearValue clear{};
    clear.color.float32[0] = 0.0f;
    clear.color.float32[1] = 0.0f;
    clear.color.float32[2] = 0.0f;
    clear.color.float32[3] = 1.0f;

    VkRenderPassBeginInfo rpbi{};
    rpbi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    rpbi.renderPass = render_pass_;
    rpbi.framebuffer = framebuffer_;
    rpbi.renderArea.offset = {0, 0};
    rpbi.renderArea.extent = {cfg_.width, cfg_.height};
    rpbi.clearValueCount = 1;
    rpbi.pClearValues = &clear;

    vkCmdBeginRenderPass(
        command_buffer_,
        &rpbi,
        VK_SUBPASS_CONTENTS_INLINE
    );

    vkCmdBindPipeline(
        command_buffer_,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        pipeline_
    );

    vkCmdBindDescriptorSets(
        command_buffer_,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
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
        VK_SHADER_STAGE_FRAGMENT_BIT,
        0,
        sizeof(PushConstants),
        &pc
    );

    vkCmdDraw(command_buffer_, 3, 1, 0, 0);

    vkCmdEndRenderPass(command_buffer_);

    CmdTransitionImage(
        command_buffer_,
        color_image_,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
        VK_ACCESS_TRANSFER_READ_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT
    );

    color_layout_ = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

    VkBufferImageCopy copy{};
    copy.bufferOffset = 0;
    copy.bufferRowLength = 0;
    copy.bufferImageHeight = 0;
    copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy.imageSubresource.mipLevel = 0;
    copy.imageSubresource.baseArrayLayer = 0;
    copy.imageSubresource.layerCount = 1;
    copy.imageOffset = {0, 0, 0};
    copy.imageExtent = {cfg_.width, cfg_.height, 1};

    vkCmdCopyImageToBuffer(
        command_buffer_,
        color_image_,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        readback_buffer_,
        1,
        &copy
    );

    VkBufferMemoryBarrier bb{};
    bb.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    bb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    bb.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    bb.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    bb.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    bb.buffer = readback_buffer_;
    bb.offset = 0;
    bb.size = readback_size_;

    vkCmdPipelineBarrier(
        command_buffer_,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_HOST_BIT,
        0,
        0,
        nullptr,
        1,
        &bb,
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

bool VulkanGraphicsBackend::WaitIdleOrFrameDone(
    uint64_t timeout_ns,
    std::string& error
) {
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
        uint64_t ts[2] = {};

        r = vkGetQueryPoolResults(
            device_,
            query_pool_,
            0,
            2,
            sizeof(ts),
            ts,
            sizeof(uint64_t),
            VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT
        );

        if (r == VK_SUCCESS && ts[1] >= ts[0]) {
            last_gpu_time_ms_ =
                static_cast<double>(ts[1] - ts[0]) *
                timestamp_period_ns_ /
                1000000.0;
        } else {
            last_gpu_time_ms_ = 0.0;
        }
    }

    return true;
}

bool VulkanGraphicsBackend::Readback(
    ReadbackBuffer& out,
    std::string& error
) {
    last_status_ = SubmitStatus::Ok;

    if (submitted_) {
        if (!WaitIdleOrFrameDone(UINT64_MAX, error)) {
            return false;
        }
    }

    if (!readback_mapped_) {
        error = "readback buffer is not mapped";
        last_status_ = SubmitStatus::ApiError;
        return false;
    }

    out.width = cfg_.width;
    out.height = cfg_.height;
    out.format = cfg_.rt_format;
    out.data.resize(static_cast<size_t>(readback_size_));

    std::memcpy(
        out.data.data(),
        readback_mapped_,
        static_cast<size_t>(readback_size_)
    );

    return true;
}

bool VulkanGraphicsBackend::SupportsGpuTimestamp() const {
    return timestamp_supported_ && query_pool_ != VK_NULL_HANDLE;
}

bool VulkanGraphicsBackend::GetLastGpuTimeMs(double& out_ms) {
    if (!SupportsGpuTimestamp()) {
        return false;
    }

    out_ms = last_gpu_time_ms_;
    return true;
}

void VulkanGraphicsBackend::Cleanup() {
    if (device_ != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device_);
    }

    if (readback_mapped_ && readback_memory_ != VK_NULL_HANDLE) {
        vkUnmapMemory(device_, readback_memory_);
        readback_mapped_ = nullptr;
    }

    if (query_pool_ != VK_NULL_HANDLE) {
        vkDestroyQueryPool(device_, query_pool_, nullptr);
        query_pool_ = VK_NULL_HANDLE;
    }

    if (pipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(device_, pipeline_, nullptr);
        pipeline_ = VK_NULL_HANDLE;
    }

    if (pipeline_layout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device_, pipeline_layout_, nullptr);
        pipeline_layout_ = VK_NULL_HANDLE;
    }

    if (framebuffer_ != VK_NULL_HANDLE) {
        vkDestroyFramebuffer(device_, framebuffer_, nullptr);
        framebuffer_ = VK_NULL_HANDLE;
    }

    if (render_pass_ != VK_NULL_HANDLE) {
        vkDestroyRenderPass(device_, render_pass_, nullptr);
        render_pass_ = VK_NULL_HANDLE;
    }

    if (descriptor_pool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device_, descriptor_pool_, nullptr);
        descriptor_pool_ = VK_NULL_HANDLE;
    }

    if (descriptor_set_layout_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device_, descriptor_set_layout_, nullptr);
        descriptor_set_layout_ = VK_NULL_HANDLE;
    }

    if (sampler_ != VK_NULL_HANDLE) {
        vkDestroySampler(device_, sampler_, nullptr);
        sampler_ = VK_NULL_HANDLE;
    }

    if (texture_view_ != VK_NULL_HANDLE) {
        vkDestroyImageView(device_, texture_view_, nullptr);
        texture_view_ = VK_NULL_HANDLE;
    }

    if (texture_image_ != VK_NULL_HANDLE) {
        vkDestroyImage(device_, texture_image_, nullptr);
        texture_image_ = VK_NULL_HANDLE;
    }

    if (texture_memory_ != VK_NULL_HANDLE) {
        vkFreeMemory(device_, texture_memory_, nullptr);
        texture_memory_ = VK_NULL_HANDLE;
    }

    if (color_view_ != VK_NULL_HANDLE) {
        vkDestroyImageView(device_, color_view_, nullptr);
        color_view_ = VK_NULL_HANDLE;
    }

    if (color_image_ != VK_NULL_HANDLE) {
        vkDestroyImage(device_, color_image_, nullptr);
        color_image_ = VK_NULL_HANDLE;
    }

    if (color_memory_ != VK_NULL_HANDLE) {
        vkFreeMemory(device_, color_memory_, nullptr);
        color_memory_ = VK_NULL_HANDLE;
    }

    if (readback_buffer_ != VK_NULL_HANDLE) {
        vkDestroyBuffer(device_, readback_buffer_, nullptr);
        readback_buffer_ = VK_NULL_HANDLE;
    }

    if (readback_memory_ != VK_NULL_HANDLE) {
        vkFreeMemory(device_, readback_memory_, nullptr);
        readback_memory_ = VK_NULL_HANDLE;
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

    if (device_ != VK_NULL_HANDLE) {
        vkDestroyDevice(device_, nullptr);
        device_ = VK_NULL_HANDLE;
    }

    if (instance_ != VK_NULL_HANDLE) {
        vkDestroyInstance(instance_, nullptr);
        instance_ = VK_NULL_HANDLE;
    }
}

void VulkanGraphicsBackend::Destroy() {
    Cleanup();

    physical_device_ = VK_NULL_HANDLE;
    queue_ = VK_NULL_HANDLE;
    queue_family_index_ = 0;

    color_layout_ = VK_IMAGE_LAYOUT_UNDEFINED;
    readback_size_ = 0;

    timestamp_supported_ = false;
    timestamp_period_ns_ = 1.0;
    last_gpu_time_ms_ = 0.0;

    submitted_ = false;
    resources_created_ = false;
    last_status_ = SubmitStatus::Ok;
}

} // namespace gpu_avs
