#pragma once

#include "Graphics/GpuTimingFrameTracker.h"

#include <array>
#include <cstdint>

namespace RHI
{
    constexpr uint32_t kDX12GpuTimingQueriesPerSlot = 2u + 2u * kMaxGpuTimingScopes;

    constexpr auto dx12_timestamp_ms(
        uint64_t begin_timestamp,
        uint64_t end_timestamp,
        uint64_t timestamp_frequency) -> double
    {
        return timestamp_frequency == 0u
            ? 0.0
            : static_cast<double>(end_timestamp - begin_timestamp) * 1000.0 /
                static_cast<double>(timestamp_frequency);
    }

    constexpr auto dx12_validate_timestamp_frequency(uint64_t timestamp_frequency) -> GpuTimingResult
    {
        return timestamp_frequency == 0u
            ? GpuTimingResult::QueueFrequencyInvalid
            : GpuTimingResult::Success;
    }

    class DX12GpuTiming final : public IGpuTimingContext
    {
    public:
        auto init(void* device, void* graphics_queue) -> GpuTimingResult;
        auto shutdown() -> void;

        auto begin_frame(CommandBuffer* cmd, uint64_t submitted_frame_index) -> GpuTimingResult override;
        auto begin_scope(
            CommandBuffer* cmd,
            uint64_t stable_name_hash,
            GpuTimingScopeHandle& out_handle) -> GpuTimingResult override;
        auto end_scope(CommandBuffer* cmd, const GpuTimingScopeHandle& handle) -> GpuTimingResult override;
        auto end_frame(CommandBuffer* cmd) -> GpuTimingResult override;
        auto try_collect(GpuTimingFrameSnapshot& out_snapshot) -> GpuTimingResult override;

        auto mark_frame_submitted(
            uint64_t frame_completion_value,
            void* completion_fence,
            bool command_buffer_submitted) -> GpuTimingResult;
        auto fail_frame_recording(GpuTimingResult failure) -> GpuTimingResult;
        auto cancel_unsubmitted_frame() -> GpuTimingResult;
        auto active_command_buffer() const -> void*;
        auto has_active_frame() const -> bool { return active_slot() != nullptr; }

    private:
        enum class ScopeState : uint8_t
        {
            Idle,
            Recording,
            Ended
        };

        struct ScopeRuntime
        {
            uint64_t stable_name_hash = 0;
            ScopeState state = ScopeState::Idle;
        };

        struct SlotRuntime
        {
            GpuTimingFrameHandle handle{};
            void* command_buffer = nullptr;
            void* completion_fence = nullptr;
            uint64_t submitted_frame_index = 0;
            uint64_t completion_value = 0;
            uint32_t scope_count = 0;
            bool recording = false;
            bool frame_ended = false;
            bool submitted = false;
            bool overflowed = false;
            std::array<ScopeRuntime, kMaxGpuTimingScopes> scopes{};
        };

        auto release_native_resources() -> void;
        auto fail_slot(uint32_t slot_index, GpuTimingResult failure) -> GpuTimingResult;
        auto clear_slot(uint32_t slot_index) -> void;
        auto find_oldest_submitted_slot() const -> uint32_t;
        auto materialize_slot(uint32_t slot_index) -> GpuTimingResult;
        auto poll_completed_slots() -> GpuTimingResult;
        auto active_slot() -> SlotRuntime*;
        auto active_slot() const -> const SlotRuntime*;

        GpuTimingFrameTracker m_tracker{};
        std::array<SlotRuntime, kGpuTimingFrameSlotCount> m_slots{};
        void* m_query_heap = nullptr;
        void* m_readback_resource = nullptr;
        void* m_mapped_readback = nullptr;
        uint64_t m_timestamp_frequency = 0;
        uint32_t m_active_slot_index = kGpuTimingFrameSlotCount;
        GpuTimingResult m_initialization_result = GpuTimingResult::Unsupported;
    };
}
