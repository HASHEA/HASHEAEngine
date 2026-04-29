#pragma once
// DX12 implementation of RHI::IGpuProfilerContext (Tracy backend).
//
// 头文件不 include TracyD3D12.hpp，避免 Tracy 内部宏污染整个 Engine 模块。

#include "Base/hplatform.h"
#include "Graphics/GpuProfilerRHI.h"
#include "DX12Wrapper.h"

namespace tracy
{
    class D3D12QueueCtx; // 前向声明
}

namespace RHI
{
    class DX12GpuProfiler final : public IGpuProfilerContext
    {
    public:
        DX12GpuProfiler(ID3D12Device* device, ID3D12CommandQueue* queue, const char* ctx_name);
        ~DX12GpuProfiler() override;

        DX12GpuProfiler(const DX12GpuProfiler&)            = delete;
        DX12GpuProfiler& operator=(const DX12GpuProfiler&) = delete;

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
        tracy::D3D12QueueCtx* m_tracy_ctx = nullptr;
    };
}
