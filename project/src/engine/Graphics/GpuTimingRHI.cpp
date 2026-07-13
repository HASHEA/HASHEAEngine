#include "Graphics/GpuTimingRHI.h"

namespace RHI
{
    namespace
    {
        IGpuTimingContext* g_gpu_timing_context = nullptr;
    }

    auto gpu_timing_install(IGpuTimingContext* context) -> void
    {
        g_gpu_timing_context = context;
    }

    auto gpu_timing_get() -> IGpuTimingContext*
    {
        return g_gpu_timing_context;
    }
}
