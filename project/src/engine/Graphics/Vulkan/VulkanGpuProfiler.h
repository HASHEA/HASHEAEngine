#pragma once
// Vulkan implementation of RHI::IGpuProfilerContext (Tracy backend).
//
// 头文件刻意不 include Tracy / TracyVulkan.hpp —— 那些只在 .cpp 内出现，
// 避免 Tracy 内部宏污染整个 Engine 模块。

#include "Base/hplatform.h"
#include "Graphics/GpuProfilerRHI.h"
#include "VulkanWrapper.h"

namespace tracy
{
    class VkCtx; // 前向声明，避免拉 TracyVulkan.hpp 进头
}

namespace RHI
{
    class VulkanGpuProfiler final : public IGpuProfilerContext
    {
    public:
        VulkanGpuProfiler(
            VkInstance       instance,
            VkPhysicalDevice physdev,
            VkDevice         device,
            VkQueue          queue,
            uint32_t         queue_family,
            const char*      ctx_name);

        ~VulkanGpuProfiler() override;

        VulkanGpuProfiler(const VulkanGpuProfiler&)            = delete;
        VulkanGpuProfiler& operator=(const VulkanGpuProfiler&) = delete;

        auto ensure_initialized(CommandBuffer* cmd) -> bool override;
        auto collect(CommandBuffer* cmd) -> void override;

        auto begin_zone(
            CommandBuffer*       cmd,
            const char*          name,
            const char*          file,
            uint32_t             line,
            const char*          function,
            uint32_t             color,
            GpuProfileZoneHandle& out_handle) -> void override;

        auto end_zone(
            CommandBuffer*              cmd,
            const GpuProfileZoneHandle& handle) -> void override;

    private:
        tracy::VkCtx* m_tracy_ctx = nullptr;
    };
}
