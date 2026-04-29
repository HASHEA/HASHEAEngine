#pragma once
// AshEngine GPU profile facade.
//
// 把 Tracy 的 GPU zone 接口抽象到一个跨 RHI 的接口里，避免在 RenderDevice/
// SceneRenderer 里直接 include Tracy 或后端头文件。
//
// 调用约定：
//   - 后端在 GraphicsContext::init() 时创建并注册自己的 IGpuProfilerContext。
//   - 每帧末尾后端在拥有空闲 cmdbuf 时调用 ctx->collect()。
//   - 引擎热路径用 ASH_GPU_PROFILE_SCOPE_* 宏在 cmdbuf 上插 zone。
//
// 注意：这里只暴露纯虚接口和 RAII 守卫，不引入 Tracy 头。
//       Tracy 的实现细节由后端 .cpp 持有。

#include "Base/hplatform.h"
#include "Base/hprofiler.h"

namespace RHI
{
    class CommandBuffer;

    // 单个 zone 在后端侧需要保存的最少状态。
    // 我们用一个不透明的 64bit 句柄来兜底（足够装一个 query index 或一个指针）。
    struct GpuProfileZoneHandle
    {
        uint64_t opaque_a = 0;
        uint64_t opaque_b = 0;
    };

    // 后端实现的 GPU profiler 上下文。Backend 会在 init() 时 install。
    class IGpuProfilerContext
    {
    public:
        virtual ~IGpuProfilerContext() = default;

        // 在第一次拿到 cmdbuf 时延迟初始化 timestamp query pool / TracyVkCtx。
        virtual auto ensure_initialized(CommandBuffer* cmd) -> bool = 0;

        // 每帧调用一次。Vulkan 端在 begin_frame 后；D3D12 端在 end_frame 前。
        // 内部把 GPU 端 timestamp 数据回收并发往 Tracy。
        virtual auto collect(CommandBuffer* cmd) -> void = 0;

        // 开/关 zone。name/file/function 是静态 / Tracy 可重复使用的字符串字面量。
        virtual auto begin_zone(
            CommandBuffer*       cmd,
            const char*          name,
            const char*          file,
            uint32_t             line,
            const char*          function,
            uint32_t             color,
            GpuProfileZoneHandle& out_handle) -> void = 0;

        virtual auto end_zone(
            CommandBuffer*              cmd,
            const GpuProfileZoneHandle& handle) -> void = 0;
    };

    // 全局获取/安装。Backend init/shutdown 各调用一次。
    auto gpu_profiler_install(IGpuProfilerContext* ctx) -> void;
    auto gpu_profiler_get() -> IGpuProfilerContext*;

    // RAII 守卫。允许 cmd 为 nullptr / ctx 未安装 — 退化为 no-op。
    class GpuProfileZoneScope
    {
    public:
        GpuProfileZoneScope(
            CommandBuffer* cmd,
            const char*    name,
            const char*    file,
            uint32_t       line,
            const char*    function,
            uint32_t       color)
            : m_cmd(cmd)
            , m_ctx(gpu_profiler_get())
        {
            if (m_cmd && m_ctx)
            {
                m_ctx->begin_zone(m_cmd, name, file, line, function, color, m_handle);
                m_active = true;
            }
        }

        ~GpuProfileZoneScope()
        {
            if (m_active && m_ctx && m_cmd)
            {
                m_ctx->end_zone(m_cmd, m_handle);
            }
        }

        GpuProfileZoneScope(const GpuProfileZoneScope&) = delete;
        GpuProfileZoneScope& operator=(const GpuProfileZoneScope&) = delete;

    private:
        CommandBuffer*       m_cmd    = nullptr;
        IGpuProfilerContext* m_ctx    = nullptr;
        GpuProfileZoneHandle m_handle{};
        bool                 m_active = false;
    };
}

// === 用户面宏 ===
//
// ASH_GPU_PROFILE_SCOPE(cmd)              — 默认名 = __FUNCTION__
// ASH_GPU_PROFILE_SCOPE_N(cmd, "name")
// ASH_GPU_PROFILE_SCOPE_NC(cmd, "name", color)
// ASH_GPU_PROFILE_COLLECT(cmd)            — 后端 .cpp 内每帧一次

#ifndef ASH_CONCAT
    #define ASH_CONCAT_INNER(a, b) a##b
    #define ASH_CONCAT(a, b)       ASH_CONCAT_INNER(a, b)
#endif

#if defined(TRACY_ENABLE)
    #define ASH_GPU_PROFILE_SCOPE(cmd) \
        ::RHI::GpuProfileZoneScope ASH_CONCAT(_ash_gpu_zone_, __LINE__)( \
            (cmd), __FUNCTION__, __FILE__, static_cast<uint32_t>(__LINE__), __FUNCTION__, 0u)

    #define ASH_GPU_PROFILE_SCOPE_N(cmd, name) \
        ::RHI::GpuProfileZoneScope ASH_CONCAT(_ash_gpu_zone_, __LINE__)( \
            (cmd), (name), __FILE__, static_cast<uint32_t>(__LINE__), __FUNCTION__, 0u)

    #define ASH_GPU_PROFILE_SCOPE_NC(cmd, name, color) \
        ::RHI::GpuProfileZoneScope ASH_CONCAT(_ash_gpu_zone_, __LINE__)( \
            (cmd), (name), __FILE__, static_cast<uint32_t>(__LINE__), __FUNCTION__, static_cast<uint32_t>(color))

    #define ASH_GPU_PROFILE_COLLECT(cmd) \
        do { \
            ::RHI::IGpuProfilerContext* _ash_gpu_ctx = ::RHI::gpu_profiler_get(); \
            if (_ash_gpu_ctx && (cmd)) { _ash_gpu_ctx->collect((cmd)); } \
        } while (0)
#else
    #define ASH_GPU_PROFILE_SCOPE(cmd)                ((void)0)
    #define ASH_GPU_PROFILE_SCOPE_N(cmd, name)        ((void)0)
    #define ASH_GPU_PROFILE_SCOPE_NC(cmd, name, color) ((void)0)
    #define ASH_GPU_PROFILE_COLLECT(cmd)              ((void)0)
#endif
