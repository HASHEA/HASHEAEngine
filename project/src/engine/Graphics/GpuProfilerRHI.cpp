#include "Graphics/GpuProfilerRHI.h"

namespace RHI
{
    namespace
    {
        IGpuProfilerContext* g_gpu_profiler = nullptr;
    }

    auto gpu_profiler_install(IGpuProfilerContext* ctx) -> void
    {
        g_gpu_profiler = ctx;
    }

    auto gpu_profiler_get() -> IGpuProfilerContext*
    {
        return g_gpu_profiler;
    }
}
