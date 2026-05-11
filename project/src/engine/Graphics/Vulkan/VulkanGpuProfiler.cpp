#include "VulkanGpuProfiler.h"
#include "VulkanCommandBuffer.h"

#if defined(TRACY_ENABLE)
    #include <tracy/TracyVulkan.hpp>
#endif

#include <cstring>

namespace RHI
{
#if defined(TRACY_ENABLE)

    namespace
    {
        // 把 RHI::CommandBuffer* 拆出底层 VkCommandBuffer。
        // 任何不是 VulkanCommandBuffer 的情况返回 VK_NULL_HANDLE，调用方应当退化为 no-op。
        inline auto unwrap_cmd(CommandBuffer* cmd) -> VkCommandBuffer
        {
            if (!cmd) return VK_NULL_HANDLE;
            auto* vkCmd = static_cast<VulkanCommandBuffer*>(cmd);
            return vkCmd->get_vkCommandBuffer();
        }
    }

    VulkanGpuProfiler::VulkanGpuProfiler(
        VkInstance       /*instance*/,
        VkPhysicalDevice physdev,
        VkDevice         device,
        VkQueue          queue,
        uint32_t         queue_family,
        const char*      ctx_name)
    {
        // TracyVkContext 需要一个临时 cmdbuf 来做一次 calibration。
        // 这里临时建一个 transient pool / buffer，初始化完成后立刻销毁。
        VkCommandPool transient_pool = VK_NULL_HANDLE;
        VkCommandPoolCreateInfo poolInfo{ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
        poolInfo.flags            = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT |
                                    VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        poolInfo.queueFamilyIndex = queue_family;
        if (vkCreateCommandPool(device, &poolInfo, nullptr, &transient_pool) != VK_SUCCESS)
        {
            return;
        }

        VkCommandBuffer cmdbuf = VK_NULL_HANDLE;
        VkCommandBufferAllocateInfo allocInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
        allocInfo.commandPool        = transient_pool;
        allocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 1;
        if (vkAllocateCommandBuffers(device, &allocInfo, &cmdbuf) != VK_SUCCESS)
        {
            vkDestroyCommandPool(device, transient_pool, nullptr);
            return;
        }

        // TracyVkContext 内部会自己 begin/submit/wait 这个 cmdbuf。
        m_tracy_ctx = TracyVkContext(physdev, device, queue, cmdbuf);

        if (m_tracy_ctx && ctx_name)
        {
            const auto len = static_cast<uint16_t>(std::strlen(ctx_name));
            TracyVkContextName(m_tracy_ctx, ctx_name, len);
        }

        vkFreeCommandBuffers(device, transient_pool, 1, &cmdbuf);
        vkDestroyCommandPool(device, transient_pool, nullptr);
    }

    VulkanGpuProfiler::~VulkanGpuProfiler()
    {
        if (m_tracy_ctx)
        {
            TracyVkDestroy(m_tracy_ctx);
            m_tracy_ctx = nullptr;
        }
    }

    auto VulkanGpuProfiler::ensure_initialized(CommandBuffer* /*cmd*/) -> bool
    {
        return m_tracy_ctx != nullptr;
    }

    auto VulkanGpuProfiler::collect(CommandBuffer* cmd) -> void
    {
        if (!m_tracy_ctx) return;
        VkCommandBuffer vkcmd = unwrap_cmd(cmd);
        if (vkcmd == VK_NULL_HANDLE) return;
        TracyVkCollect(m_tracy_ctx, vkcmd);
    }

    auto VulkanGpuProfiler::begin_zone(
        CommandBuffer*       cmd,
        const char*          name,
        const char*          file,
        uint32_t             line,
        const char*          function,
        uint32_t             /*color*/,
        GpuProfileZoneHandle& out_handle) -> void
    {
        out_handle = {};
        if (!m_tracy_ctx) return;

        VkCommandBuffer vkcmd = unwrap_cmd(cmd);
        if (vkcmd == VK_NULL_HANDLE) return;

        // 单个 zone 在 GPU profiler 路径上每帧只命中几次（per-pass），
        // 这里用 new/delete 兜底足够。VkCtxScope 只持两个指针 + 一个 bool，
        // 走 tracy_malloc 也没本质差别。
        const char*  src_name = name     ? name     : "GPU";
        const char*  src_file = file     ? file     : "";
        const char*  src_func = function ? function : "";
        const size_t name_sz  = std::strlen(src_name);
        const size_t file_sz  = std::strlen(src_file);
        const size_t func_sz  = std::strlen(src_func);

        auto* scope = new tracy::VkCtxScope(
            m_tracy_ctx,
            line,
            src_file, file_sz,
            src_func, func_sz,
            src_name, name_sz,
            vkcmd,
            true);

        out_handle.opaque_a = reinterpret_cast<uint64_t>(scope);
    }

    auto VulkanGpuProfiler::end_zone(
        CommandBuffer*              /*cmd*/,
        const GpuProfileZoneHandle& handle) -> void
    {
        auto* scope = reinterpret_cast<tracy::VkCtxScope*>(handle.opaque_a);
        if (scope)
        {
            // VkCtxScope 析构时记录 end timestamp + 入队 zone-end 事件。
            delete scope;
        }
    }

#else // !TRACY_ENABLE

    VulkanGpuProfiler::VulkanGpuProfiler(
        VkInstance, VkPhysicalDevice, VkDevice, VkQueue, uint32_t, const char*)
    {
    }

    VulkanGpuProfiler::~VulkanGpuProfiler() = default;

    auto VulkanGpuProfiler::ensure_initialized(CommandBuffer*) -> bool { return false; }
    auto VulkanGpuProfiler::collect(CommandBuffer*) -> void {}
    auto VulkanGpuProfiler::begin_zone(
        CommandBuffer*, const char*, const char*, uint32_t,
        const char*, uint32_t, GpuProfileZoneHandle& out_handle) -> void
    {
        out_handle = {};
    }
    auto VulkanGpuProfiler::end_zone(CommandBuffer*, const GpuProfileZoneHandle&) -> void {}

#endif
}
