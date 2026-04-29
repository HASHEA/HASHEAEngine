#include "DX12GpuProfiler.h"
#include "DX12CommandBuffer.h"

#if defined(TRACY_ENABLE)
    #include <tracy/TracyD3D12.hpp>
#endif

#include <cstring>

namespace RHI
{
#if defined(TRACY_ENABLE)

    namespace
    {
        inline auto unwrap_cmd(CommandBuffer* cmd) -> ID3D12GraphicsCommandList*
        {
            if (!cmd) return nullptr;
            auto* dxCmd = static_cast<DX12CommandBuffer*>(cmd);
            return dxCmd->get_command_list();
        }
    }

    DX12GpuProfiler::DX12GpuProfiler(ID3D12Device* device, ID3D12CommandQueue* queue, const char* ctx_name)
    {
        m_tracy_ctx = TracyD3D12Context(device, queue);
        if (m_tracy_ctx && ctx_name)
        {
            const auto len = static_cast<uint16_t>(std::strlen(ctx_name));
            TracyD3D12ContextName(m_tracy_ctx, ctx_name, len);
        }
    }

    DX12GpuProfiler::~DX12GpuProfiler()
    {
        if (m_tracy_ctx)
        {
            TracyD3D12Destroy(m_tracy_ctx);
            m_tracy_ctx = nullptr;
        }
    }

    auto DX12GpuProfiler::ensure_initialized(CommandBuffer* /*cmd*/) -> bool
    {
        return m_tracy_ctx != nullptr;
    }

    auto DX12GpuProfiler::collect(CommandBuffer* /*cmd*/) -> void
    {
        if (!m_tracy_ctx) return;
        // Tracy 的 D3D12 collect 自带 NewFrame()。这里两个都调，便于 Tracy 正确分帧。
        TracyD3D12NewFrame(m_tracy_ctx);
        TracyD3D12Collect(m_tracy_ctx);
    }

    auto DX12GpuProfiler::begin_zone(
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

        ID3D12GraphicsCommandList* cmdList = unwrap_cmd(cmd);
        if (!cmdList) return;

        const char*  src_name = name     ? name     : "GPU";
        const char*  src_file = file     ? file     : "";
        const char*  src_func = function ? function : "";
        const size_t name_sz  = std::strlen(src_name);
        const size_t file_sz  = std::strlen(src_file);
        const size_t func_sz  = std::strlen(src_func);

        auto* scope = new tracy::D3D12ZoneScope(
            m_tracy_ctx,
            line,
            src_file, file_sz,
            src_func, func_sz,
            src_name, name_sz,
            cmdList,
            true);

        out_handle.opaque_a = reinterpret_cast<uint64_t>(scope);
    }

    auto DX12GpuProfiler::end_zone(
        CommandBuffer*              /*cmd*/,
        const GpuProfileZoneHandle& handle) -> void
    {
        auto* scope = reinterpret_cast<tracy::D3D12ZoneScope*>(handle.opaque_a);
        if (scope)
        {
            delete scope;
        }
    }

#else // !TRACY_ENABLE

    DX12GpuProfiler::DX12GpuProfiler(ID3D12Device*, ID3D12CommandQueue*, const char*) {}
    DX12GpuProfiler::~DX12GpuProfiler() = default;
    auto DX12GpuProfiler::ensure_initialized(CommandBuffer*) -> bool { return false; }
    auto DX12GpuProfiler::collect(CommandBuffer*) -> void {}
    auto DX12GpuProfiler::begin_zone(
        CommandBuffer*, const char*, const char*, uint32_t,
        const char*, uint32_t, GpuProfileZoneHandle& out_handle) -> void
    {
        out_handle = {};
    }
    auto DX12GpuProfiler::end_zone(CommandBuffer*, const GpuProfileZoneHandle&) -> void {}

#endif
}
