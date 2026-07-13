#pragma once

#include "Graphics/GpuTimingRHI.h"

#include <array>
#include <cstdint>

namespace RHI
{
    constexpr uint32_t kGpuTimingFrameSlotCount = 4;
    constexpr uint32_t kGpuTimingSnapshotFifoCapacity = 8;

    struct GpuTimingFrameHandle
    {
        uint32_t slot_index = 0;
        uint64_t generation = 0;

        friend constexpr auto operator==(const GpuTimingFrameHandle& lhs, const GpuTimingFrameHandle& rhs) -> bool
        {
            return lhs.slot_index == rhs.slot_index && lhs.generation == rhs.generation;
        }

        friend constexpr auto operator!=(const GpuTimingFrameHandle& lhs, const GpuTimingFrameHandle& rhs) -> bool
        {
            return !(lhs == rhs);
        }
    };

    struct GpuTimingSubmissionInfo
    {
        uint64_t submitted_frame_index = 0;
        uint64_t completion_value = 0;
        uint32_t query_count = 0;
    };

    // submitted_frame_index and each slot's uint64_t generation are non-wrapping
    // values during one tracker/context lifetime. Frame ordering intentionally uses
    // raw uint64_t comparison, so a wrapped frame index is rejected as InvalidState;
    // generation wrap is outside the supported context lifetime.
    class GpuTimingFrameTracker
    {
    public:
        auto begin_recording(uint64_t submitted_frame_index, GpuTimingFrameHandle& out_handle) -> GpuTimingResult
        {
            if (m_has_latest_frame_index && submitted_frame_index <= m_latest_frame_index)
            {
                return GpuTimingResult::InvalidState;
            }

            for (uint32_t slot_index = 0; slot_index < kGpuTimingFrameSlotCount; ++slot_index)
            {
                FrameSlot& slot = m_slots[slot_index];
                if (slot.state != FrameSlotState::Idle)
                {
                    continue;
                }

                ++slot.generation;
                if (slot.generation == 0)
                {
                    ++slot.generation;
                }

                slot.state = FrameSlotState::Recording;
                slot.submitted_frame_index = submitted_frame_index;
                slot.completion_value = 0;
                slot.query_count = 0;
                m_latest_frame_index = submitted_frame_index;
                m_has_latest_frame_index = true;
                out_handle = { slot_index, slot.generation };
                return GpuTimingResult::Success;
            }

            return GpuTimingResult::CapacityExceeded;
        }

        auto set_query_count(const GpuTimingFrameHandle& handle, uint32_t query_count) -> GpuTimingResult
        {
            FrameSlot* slot = nullptr;
            const GpuTimingResult validation = validate_handle(handle, slot);
            if (validation != GpuTimingResult::Success)
            {
                return validation;
            }
            if (slot->state != FrameSlotState::Recording)
            {
                return GpuTimingResult::InvalidState;
            }
            if (query_count > 2u + 2u * kMaxGpuTimingScopes)
            {
                return GpuTimingResult::CapacityExceeded;
            }

            slot->query_count = query_count;
            return GpuTimingResult::Success;
        }

        auto mark_submitted(const GpuTimingFrameHandle& handle, uint64_t completion_value) -> GpuTimingResult
        {
            FrameSlot* slot = nullptr;
            const GpuTimingResult validation = validate_handle(handle, slot);
            if (validation != GpuTimingResult::Success)
            {
                return validation;
            }
            if (slot->state != FrameSlotState::Recording)
            {
                return GpuTimingResult::InvalidState;
            }
            if (find_oldest_recording_slot() != handle.slot_index)
            {
                return GpuTimingResult::InvalidState;
            }

            slot->completion_value = completion_value;
            slot->state = FrameSlotState::Submitted;
            return GpuTimingResult::Success;
        }

        auto get_submission(
            const GpuTimingFrameHandle& handle,
            GpuTimingSubmissionInfo& out_submission) const -> GpuTimingResult
        {
            const FrameSlot* slot = nullptr;
            const GpuTimingResult validation = validate_handle(handle, slot);
            if (validation != GpuTimingResult::Success)
            {
                return validation;
            }
            if (slot->state != FrameSlotState::Submitted && slot->state != FrameSlotState::Completed)
            {
                return GpuTimingResult::InvalidState;
            }

            out_submission = { slot->submitted_frame_index, slot->completion_value, slot->query_count };
            return GpuTimingResult::Success;
        }

        auto mark_completed(
            const GpuTimingFrameHandle& handle,
            const GpuTimingFrameSnapshot& snapshot) -> GpuTimingResult
        {
            FrameSlot* slot = nullptr;
            const GpuTimingResult validation = validate_handle(handle, slot);
            if (validation != GpuTimingResult::Success)
            {
                return validation;
            }
            if (slot->state != FrameSlotState::Submitted ||
                snapshot.submitted_frame_index != slot->submitted_frame_index)
            {
                return GpuTimingResult::InvalidState;
            }
            if (find_oldest_submitted_slot() != handle.slot_index)
            {
                return GpuTimingResult::InvalidState;
            }

            slot->state = FrameSlotState::Completed;
            if (m_fifo_count == kGpuTimingSnapshotFifoCapacity)
            {
                slot->state = FrameSlotState::Failed;
                set_sticky_failure(GpuTimingResult::CapacityExceeded);
                release_slot(*slot);
                return GpuTimingResult::CapacityExceeded;
            }

            const uint32_t tail = (m_fifo_head + m_fifo_count) % kGpuTimingSnapshotFifoCapacity;
            SnapshotEntry& entry = m_snapshot_fifo[tail];
            entry.snapshot = snapshot;
            entry.state = SnapshotState::Queued;
            ++m_fifo_count;

            slot->state = FrameSlotState::Materialized;
            release_slot(*slot);
            return GpuTimingResult::Success;
        }

        auto cancel_recording(const GpuTimingFrameHandle& handle) -> GpuTimingResult
        {
            FrameSlot* slot = nullptr;
            const GpuTimingResult validation = validate_handle(handle, slot);
            if (validation != GpuTimingResult::Success)
            {
                return validation;
            }
            if (slot->state != FrameSlotState::Recording)
            {
                return GpuTimingResult::InvalidState;
            }

            release_slot(*slot);
            return GpuTimingResult::Success;
        }

        auto fail(const GpuTimingFrameHandle& handle, GpuTimingResult failure) -> GpuTimingResult
        {
            if (failure == GpuTimingResult::Success || failure == GpuTimingResult::Pending)
            {
                return GpuTimingResult::InvalidState;
            }

            FrameSlot* slot = nullptr;
            const GpuTimingResult validation = validate_handle(handle, slot);
            if (validation != GpuTimingResult::Success)
            {
                return validation;
            }
            if (slot->state != FrameSlotState::Recording &&
                slot->state != FrameSlotState::Submitted &&
                slot->state != FrameSlotState::Completed)
            {
                return GpuTimingResult::InvalidState;
            }

            slot->state = FrameSlotState::Failed;
            set_sticky_failure(failure);
            release_slot(*slot);
            return GpuTimingResult::Success;
        }

        auto try_publish(GpuTimingFrameSnapshot& out_snapshot) -> GpuTimingResult
        {
            if (m_fifo_count == 0)
            {
                return GpuTimingResult::Pending;
            }

            SnapshotEntry& entry = m_snapshot_fifo[m_fifo_head];
            if (entry.state != SnapshotState::Queued)
            {
                return GpuTimingResult::InvalidState;
            }

            out_snapshot = entry.snapshot;
            entry.state = SnapshotState::Published;
            entry.snapshot = {};
            entry.state = SnapshotState::Idle;
            m_fifo_head = (m_fifo_head + 1) % kGpuTimingSnapshotFifoCapacity;
            --m_fifo_count;
            return GpuTimingResult::Success;
        }

        auto sticky_failure() const -> GpuTimingResult
        {
            return m_sticky_failure;
        }

    private:
        enum class FrameSlotState : uint8_t
        {
            Idle,
            Recording,
            Submitted,
            Completed,
            Materialized,
            Failed
        };

        enum class SnapshotState : uint8_t
        {
            Idle,
            Queued,
            Published
        };

        struct FrameSlot
        {
            FrameSlotState state = FrameSlotState::Idle;
            uint64_t generation = 0;
            uint64_t submitted_frame_index = 0;
            uint64_t completion_value = 0;
            uint32_t query_count = 0;
        };

        struct SnapshotEntry
        {
            SnapshotState state = SnapshotState::Idle;
            GpuTimingFrameSnapshot snapshot{};
        };

        auto validate_handle(const GpuTimingFrameHandle& handle, FrameSlot*& out_slot) -> GpuTimingResult
        {
            if (handle.slot_index >= kGpuTimingFrameSlotCount)
            {
                return GpuTimingResult::StaleHandle;
            }

            FrameSlot& slot = m_slots[handle.slot_index];
            if (slot.generation != handle.generation)
            {
                return GpuTimingResult::StaleHandle;
            }

            out_slot = &slot;
            return GpuTimingResult::Success;
        }

        auto validate_handle(const GpuTimingFrameHandle& handle, const FrameSlot*& out_slot) const -> GpuTimingResult
        {
            if (handle.slot_index >= kGpuTimingFrameSlotCount)
            {
                return GpuTimingResult::StaleHandle;
            }

            const FrameSlot& slot = m_slots[handle.slot_index];
            if (slot.generation != handle.generation)
            {
                return GpuTimingResult::StaleHandle;
            }

            out_slot = &slot;
            return GpuTimingResult::Success;
        }

        auto find_oldest_submitted_slot() const -> uint32_t
        {
            uint32_t oldest_slot = kGpuTimingFrameSlotCount;
            uint64_t oldest_frame_index = 0;
            bool found = false;

            for (uint32_t slot_index = 0; slot_index < kGpuTimingFrameSlotCount; ++slot_index)
            {
                const FrameSlot& slot = m_slots[slot_index];
                if (slot.state != FrameSlotState::Submitted)
                {
                    continue;
                }
                if (!found || slot.submitted_frame_index < oldest_frame_index)
                {
                    oldest_slot = slot_index;
                    oldest_frame_index = slot.submitted_frame_index;
                    found = true;
                }
            }

            return oldest_slot;
        }

        auto find_oldest_recording_slot() const -> uint32_t
        {
            uint32_t oldest_slot = kGpuTimingFrameSlotCount;
            uint64_t oldest_frame_index = 0;
            bool found = false;

            for (uint32_t slot_index = 0; slot_index < kGpuTimingFrameSlotCount; ++slot_index)
            {
                const FrameSlot& slot = m_slots[slot_index];
                if (slot.state != FrameSlotState::Recording)
                {
                    continue;
                }
                if (!found || slot.submitted_frame_index < oldest_frame_index)
                {
                    oldest_slot = slot_index;
                    oldest_frame_index = slot.submitted_frame_index;
                    found = true;
                }
            }

            return oldest_slot;
        }

        static auto release_slot(FrameSlot& slot) -> void
        {
            slot.state = FrameSlotState::Idle;
            slot.submitted_frame_index = 0;
            slot.completion_value = 0;
            slot.query_count = 0;
        }

        auto set_sticky_failure(GpuTimingResult failure) -> void
        {
            if (m_sticky_failure == GpuTimingResult::Success)
            {
                m_sticky_failure = failure;
            }
        }

        std::array<FrameSlot, kGpuTimingFrameSlotCount> m_slots{};
        std::array<SnapshotEntry, kGpuTimingSnapshotFifoCapacity> m_snapshot_fifo{};
        uint64_t m_latest_frame_index = 0;
        uint32_t m_fifo_head = 0;
        uint32_t m_fifo_count = 0;
        GpuTimingResult m_sticky_failure = GpuTimingResult::Success;
        bool m_has_latest_frame_index = false;
    };
}
