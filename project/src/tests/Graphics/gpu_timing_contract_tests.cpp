#include "Graphics/GpuTimingRHI.h"

#ifdef TYPE_TO_STRING
#undef TYPE_TO_STRING
#endif
#include "doctest.h"

namespace
{
    static_assert(
        RHI::gpu_timing_name_hash(nullptr) == 14695981039346656037ull,
        "A null timing name must hash to the FNV-1a offset basis");

    class FakeGpuTimingContext final : public RHI::IGpuTimingContext
    {
    public:
        auto begin_frame(RHI::CommandBuffer*, uint64_t) -> RHI::GpuTimingResult override
        {
            return RHI::GpuTimingResult::Success;
        }

        auto begin_scope(RHI::CommandBuffer*, uint64_t, RHI::GpuTimingScopeHandle&) -> RHI::GpuTimingResult override
        {
            return RHI::GpuTimingResult::Success;
        }

        auto end_scope(RHI::CommandBuffer*, const RHI::GpuTimingScopeHandle&) -> RHI::GpuTimingResult override
        {
            return RHI::GpuTimingResult::Success;
        }

        auto end_frame(RHI::CommandBuffer*) -> RHI::GpuTimingResult override
        {
            return RHI::GpuTimingResult::Success;
        }

        auto try_collect(RHI::GpuTimingFrameSnapshot&) -> RHI::GpuTimingResult override
        {
            return RHI::GpuTimingResult::Pending;
        }
    };
}

TEST_CASE("GPU timing facade installs and clears one context")
{
    FakeGpuTimingContext fake;
    RHI::gpu_timing_install(&fake);
    CHECK(RHI::gpu_timing_get() == &fake);
    RHI::gpu_timing_install(nullptr);
    CHECK(RHI::gpu_timing_get() == nullptr);
}

TEST_CASE("GPU timing names use stable FNV-1a hashes")
{
    CHECK(RHI::gpu_timing_name_hash("Terrain.GBuffer") == 0xb740cda0611fe57bull);
    CHECK(RHI::gpu_timing_name_hash("Terrain.Shadow") != RHI::gpu_timing_name_hash("Terrain.GBuffer"));
}
