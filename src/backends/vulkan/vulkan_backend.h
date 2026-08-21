#pragma once

#include "gpu_avs/backend.h"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <string>
#include <vector>

namespace gpu_avs {

class VulkanBackend final : public IGpuBackend {
public:
    VulkanBackend() = default;
    ~VulkanBackend() override;

    bool Init(const WorkloadConfig& cfg, std::string& error) override;
    bool CreateResources(std::string& error) override;

    SubmitStatus SubmitWorkload(uint64_t frame_index) override;
    bool WaitIdleOrFrameDone(uint64_t timeout_ns, std::string& error) override;

    bool Readback(ReadbackBuffer& out, std::string& error) override;

    bool SupportsGpuTimestamp() const override;
    bool GetLastGpuTimeMs(double& out_ms) override;

    SubmitStatus LastStatus() const override { return last_status_; }

    void Destroy() override;

    const char* Name() const override { return "vulkan-compute"; }

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

    bool CreateOutputBuffer(std::string& error);
    bool CreateDescriptorResources(std::string& error);
    bool CreatePipeline(std::string& error);
    bool CreateCommandResources(std::string& error);
    bool CreateQueryPool(std::string& error);

    bool LoadShaderFile(std::vector<uint32_t>& spv, std::string& error);
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

    uint32_t ShaderId() const;

    void SetStatusFromVkResult(VkResult r);
    std::string VkResultToString(VkResult r) const;

    void CleanupVulkanObjects();

private:
    WorkloadConfig cfg_;

    VkInstance instance_ = VK_NULL_HANDLE;
    VkPhysicalDevice physical_device_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;

    VkQueue queue_ = VK_NULL_HANDLE;
    uint32_t queue_family_index_ = 0;

    VkPhysicalDeviceProperties device_props_{};

    VkBuffer output_buffer_ = VK_NULL_HANDLE;
    VkDeviceMemory output_memory_ = VK_NULL_HANDLE;
    void* output_mapped_ = nullptr;
    VkDeviceSize output_size_ = 0;

    VkDescriptorSetLayout descriptor_set_layout_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool_ = VK_NULL_HANDLE;
    VkDescriptorSet descriptor_set_ = VK_NULL_HANDLE;

    VkPipelineLayout pipeline_layout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;

    VkCommandPool command_pool_ = VK_NULL_HANDLE;
    VkCommandBuffer command_buffer_ = VK_NULL_HANDLE;
    VkFence fence_ = VK_NULL_HANDLE;

    VkQueryPool query_pool_ = VK_NULL_HANDLE;
    bool timestamp_supported_ = false;
    double timestamp_period_ns_ = 1.0;
    double last_gpu_time_ms_ = 0.0;

    bool submitted_ = false;
    bool resources_created_ = false;

    SubmitStatus last_status_ = SubmitStatus::Ok;
};

} // namespace gpu_avs
