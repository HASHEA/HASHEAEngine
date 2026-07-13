#pragma once

#include "Base/hcore.h"

#include <array>
#include <cstdint>

namespace RHI
{
    constexpr uint32_t kMaxGpuTimingScopes = 128;

    enum class GpuTimingResult : uint8_t
    {
        Success,
        Pending,
        Unsupported,
        CapacityExceeded,
        InvalidState,
        StaleHandle,
        RecordFailed,
        ResolveFailed,
        DeviceLost,
        QueueFrequencyInvalid
    };

    struct GpuTimingScopeHandle
    {
        uint32_t frame_slot = 0;
        uint32_t scope_slot = 0;
        uint64_t generation = 0;
    };

    struct GpuTimingScopeSample
    {
        uint64_t stable_name_hash = 0;
        double elapsed_ms = 0.0;
    };

    struct GpuTimingFrameSnapshot
    {
        uint64_t submitted_frame_index = 0;
        double frame_elapsed_ms = 0.0;
        uint32_t scope_count = 0;
        bool overflowed = false;
        std::array<GpuTimingScopeSample, kMaxGpuTimingScopes> scopes{};
    };

    class CommandBuffer;

    class IGpuTimingContext
    {
    public:
        virtual ~IGpuTimingContext() = default;

        virtual auto begin_frame(CommandBuffer* cmd, uint64_t submitted_frame_index) -> GpuTimingResult = 0;
        virtual auto begin_scope(
            CommandBuffer* cmd,
            uint64_t stable_name_hash,
            GpuTimingScopeHandle& out_handle) -> GpuTimingResult = 0;
        virtual auto end_scope(CommandBuffer* cmd, const GpuTimingScopeHandle& handle) -> GpuTimingResult = 0;
        virtual auto end_frame(CommandBuffer* cmd) -> GpuTimingResult = 0;
        virtual auto try_collect(GpuTimingFrameSnapshot& out_snapshot) -> GpuTimingResult = 0;
    };

    ASH_API auto gpu_timing_install(IGpuTimingContext* context) -> void;
    ASH_API auto gpu_timing_get() -> IGpuTimingContext*;

    constexpr auto gpu_timing_name_hash(const char* name) -> uint64_t
    {
        uint64_t hash = 14695981039346656037ull;
        while (name && *name)
        {
            hash ^= static_cast<uint8_t>(*name++);
            hash *= 1099511628211ull;
        }
        return hash;
    }
}
