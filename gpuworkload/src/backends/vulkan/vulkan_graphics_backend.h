#pragma once

#include "gpu_avs/backend.h"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <string>
#include <vector>

namespace gpu_avs {

class VulkanGraphicsBackend final : public IGpuBackend {
public:
    VulkanGraphicsBackend() = default;
    ~VulkanGraphicsBackend() override;

    bool Init(const WorkloadConfig& cfg, std::string& error) override;
    bool CreateResources(std::string& error) override;

    SubmitStatus SubmitWorkload(uint64_t frame_index) override;
    bool WaitIdleOrFrameDone(uint64_t timeout_ns, std::string& error) override;

    bool Readback(ReadbackBuffer& out, std::string& error) override;

    bool SupportsGpuTimestamp() const override;
    bool GetLastGpuTimeMs(double& out_ms) override;

    SubmitStatus LastStatus() const override { return last_status_; }

    void Destroy() override;

    const char* Name() const override { return "vulkan-graphics"; }

private:
    struct PushConstants {
        uint32_t width;
        uint32_t height;
        uint32_t iterations;
        uint32_t shader_id;
        uint32_t texture_count;
        uint32_t frame_index;
        uint32_t reserved0;
        uint32_t reserved1;
    };

private:
    bool CreateInstance(std::string& error);
    bool PickPhysicalDevice(std::string& error);
    bool CreateDevice(std::string& error);

    bool CreateCommandResources(std::string& error);
    bool CreateColorTarget(std::string& error);
    bool CreateReadbackBuffer(std::string& error);
    bool CreateTexture(std::string& error);
    bool CreateDescriptorResources(std::string& error);
    bool CreateRenderPass(std::string& error);
    bool CreateFramebuffer(std::string& error);
    bool CreatePipeline(std::string& error);
    bool CreateQueryPool(std::string& error);

    bool CreateBuffer(
        VkDeviceSize size,
        VkBufferUsageFlags usage,
        VkMemoryPropertyFlags mem_flags,
        VkBuffer& buffer,
        VkDeviceMemory& memory,
        void** mapped,
        std::string& error
    );

    bool CreateImage(
        uint32_t width,
        uint32_t height,
        VkFormat format,
        VkImageUsageFlags usage,
        VkImage& image,
        VkDeviceMemory& memory,
        std::string& error
    );

    bool CreateImageView(
        VkImage image,
        VkFormat format,
        VkImageView& view,
        std::string& error
    );

    bool LoadShaderFile(
        const std::string& name,
        std::vector<uint32_t>& spv,
        std::string& error
    );

    bool CreateShaderModule(
        const std::vector<uint32_t>& spv,
        VkShaderModule& module,
        std::string& error
    );

    bool FindMemoryType(
        uint32_t type_bits,
        VkMemoryPropertyFlags flags,
        uint32_t& type_index
    ) const;

    bool BeginOneTimeCommands(VkCommandBuffer& cmd, std::string& error);
    bool EndOneTimeCommands(VkCommandBuffer cmd, std::string& error);

    void CmdTransitionImage(
        VkCommandBuffer cmd,
        VkImage image,
        VkImageLayout old_layout,
        VkImageLayout new_layout,
        VkAccessFlags src_access,
        VkAccessFlags dst_access,
        VkPipelineStageFlags src_stage,
        VkPipelineStageFlags dst_stage
    );

    uint32_t ShaderId() const;
    uint32_t ParseTextureWidth() const;
    uint32_t ParseTextureHeight() const;

    void SetStatusFromVkResult(VkResult r);
    std::string VkResultToString(VkResult r) const;

    void Cleanup();

private:
    WorkloadConfig cfg_;

    VkInstance instance_ = VK_NULL_HANDLE;
    VkPhysicalDevice physical_device_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;
    uint32_t queue_family_index_ = 0;
    VkPhysicalDeviceProperties device_props_{};

    VkCommandPool command_pool_ = VK_NULL_HANDLE;
    VkCommandBuffer command_buffer_ = VK_NULL_HANDLE;
    VkFence fence_ = VK_NULL_HANDLE;

    VkFormat color_format_ = VK_FORMAT_R8G8B8A8_UNORM;

    VkImage color_image_ = VK_NULL_HANDLE;
    VkDeviceMemory color_memory_ = VK_NULL_HANDLE;
    VkImageView color_view_ = VK_NULL_HANDLE;
    VkImageLayout color_layout_ = VK_IMAGE_LAYOUT_UNDEFINED;

    VkBuffer readback_buffer_ = VK_NULL_HANDLE;
    VkDeviceMemory readback_memory_ = VK_NULL_HANDLE;
    void* readback_mapped_ = nullptr;
    VkDeviceSize readback_size_ = 0;

    VkImage texture_image_ = VK_NULL_HANDLE;
    VkDeviceMemory texture_memory_ = VK_NULL_HANDLE;
    VkImageView texture_view_ = VK_NULL_HANDLE;
    VkSampler sampler_ = VK_NULL_HANDLE;

    VkDescriptorSetLayout descriptor_set_layout_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool_ = VK_NULL_HANDLE;
    VkDescriptorSet descriptor_set_ = VK_NULL_HANDLE;

    VkRenderPass render_pass_ = VK_NULL_HANDLE;
    VkFramebuffer framebuffer_ = VK_NULL_HANDLE;

    VkPipelineLayout pipeline_layout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;

    VkQueryPool query_pool_ = VK_NULL_HANDLE;
    bool timestamp_supported_ = false;
    double timestamp_period_ns_ = 1.0;
    double last_gpu_time_ms_ = 0.0;

    bool submitted_ = false;
    bool resources_created_ = false;

    SubmitStatus last_status_ = SubmitStatus::Ok;
};

} // namespace gpu_avs
