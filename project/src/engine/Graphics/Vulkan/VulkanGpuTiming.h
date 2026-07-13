#pragma once

#include "Graphics/GpuTimingFrameTracker.h"

#include <array>
#include <cstdint>

namespace RHI
{
    constexpr uint32_t kVulkanGpuTimingQueriesPerSlot = 2u + 2u * kMaxGpuTimingScopes;

    constexpr auto vulkan_timestamp_delta(
        uint64_t begin_timestamp,
        uint64_t end_timestamp,
        uint32_t timestamp_valid_bits) -> uint64_t
    {
        if (timestamp_valid_bits == 0u)
        {
            return 0u;
        }
        if (timestamp_valid_bits >= 64u)
        {
            return end_timestamp - begin_timestamp;
        }

        const uint64_t valid_mask = (uint64_t{ 1 } << timestamp_valid_bits) - 1u;
        return (end_timestamp - begin_timestamp) & valid_mask;
    }

    class VulkanGpuTiming final : public IGpuTimingContext
    {
    public:
        auto init(
            void* physical_device,
            void* device,
            uint32_t graphics_queue_family,
            void* graphics_timeline_semaphore,
            bool synchronization2_enabled) -> GpuTimingResult;
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
            void* const* submitted_command_buffers,
            uint32_t submitted_command_buffer_count) -> GpuTimingResult;
        auto fail_frame_recording(GpuTimingResult failure) -> GpuTimingResult;
        auto resolve_timeline_completion(uint64_t completed_value) -> GpuTimingResult;
        auto resolve_fence_completion(void* completed_fence) -> GpuTimingResult;
        auto has_active_frame() const -> bool { return active_slot() != nullptr; }

    private:
        enum class CompletionKind : uint8_t
        {
            None,
            Timeline,
            Fence
        };

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
            CompletionKind completion_kind = CompletionKind::None;
            bool recording = false;
            bool frame_ended = false;
            bool submitted = false;
            bool overflowed = false;
            std::array<ScopeRuntime, kMaxGpuTimingScopes> scopes{};
        };

        struct QueryResult
        {
            uint64_t timestamp = 0;
            uint64_t available = 0;
        };

        auto write_timestamp(void* command_buffer, uint32_t query_index, bool end_point) const -> void;
        auto fail_slot(uint32_t slot_index, GpuTimingResult failure) -> GpuTimingResult;
        auto clear_slot(uint32_t slot_index) -> void;
        auto find_oldest_submitted_slot() const -> uint32_t;
        auto materialize_slot(uint32_t slot_index) -> GpuTimingResult;
        auto poll_completed_slots() -> GpuTimingResult;
        auto resolve_known_completion(
            CompletionKind kind,
            uint64_t completed_value,
            void* completed_fence) -> GpuTimingResult;
        auto active_slot() -> SlotRuntime*;
        auto active_slot() const -> const SlotRuntime*;

        GpuTimingFrameTracker m_tracker{};
        std::array<SlotRuntime, kGpuTimingFrameSlotCount> m_slots{};
        void* m_device = nullptr;
        void* m_query_pool = nullptr;
        void* m_graphics_timeline_semaphore = nullptr;
        double m_timestamp_period_ns = 0.0;
        uint32_t m_timestamp_valid_bits = 0;
        uint32_t m_active_slot_index = kGpuTimingFrameSlotCount;
        GpuTimingResult m_initialization_result = GpuTimingResult::Unsupported;
        bool m_use_synchronization2 = false;
    };
}
