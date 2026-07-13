#include "DX12GpuTiming.h"

#include "DX12Helper.hpp"
#include "DX12Wrapper.h"
#include "Graphics/CommandBuffer.h"

#include <cmath>
#include <cstddef>

namespace RHI
{
    auto DX12GpuTiming::init(void* device_handle, void* graphics_queue_handle) -> GpuTimingResult
    {
        shutdown();
        m_initialization_result = GpuTimingResult::InvalidState;
        auto* device = static_cast<ID3D12Device*>(device_handle);
        auto* graphics_queue = static_cast<ID3D12CommandQueue*>(graphics_queue_handle);
        if (!device || !graphics_queue)
        {
            return m_initialization_result;
        }

        uint64_t timestamp_frequency = 0;
        const HRESULT frequency_result = graphics_queue->GetTimestampFrequency(&timestamp_frequency);
        if (FAILED(frequency_result) ||
            dx12_validate_timestamp_frequency(timestamp_frequency) != GpuTimingResult::Success)
        {
            m_initialization_result = GpuTimingResult::QueueFrequencyInvalid;
            return m_initialization_result;
        }
        m_timestamp_frequency = timestamp_frequency;

        D3D12_QUERY_HEAP_DESC query_heap_desc{};
        query_heap_desc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
        query_heap_desc.Count = kGpuTimingFrameSlotCount * kDX12GpuTimingQueriesPerSlot;
        ID3D12QueryHeap* query_heap = nullptr;
        const HRESULT query_heap_result =
            device->CreateQueryHeap(&query_heap_desc, IID_PPV_ARGS(&query_heap));
        if (FAILED(query_heap_result) || !query_heap)
        {
            m_initialization_result = GpuTimingResult::RecordFailed;
            return m_initialization_result;
        }
        m_query_heap = query_heap;
        dx12_set_debug_name(query_heap, "DX12 Machine-readable GPU Timing Query Heap");

        D3D12_HEAP_PROPERTIES heap_properties{};
        heap_properties.Type = D3D12_HEAP_TYPE_READBACK;
        heap_properties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        heap_properties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        heap_properties.CreationNodeMask = 1u;
        heap_properties.VisibleNodeMask = 1u;

        D3D12_RESOURCE_DESC readback_desc{};
        readback_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        readback_desc.Width = static_cast<uint64_t>(query_heap_desc.Count) * sizeof(uint64_t);
        readback_desc.Height = 1u;
        readback_desc.DepthOrArraySize = 1u;
        readback_desc.MipLevels = 1u;
        readback_desc.Format = DXGI_FORMAT_UNKNOWN;
        readback_desc.SampleDesc.Count = 1u;
        readback_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        readback_desc.Flags = D3D12_RESOURCE_FLAG_NONE;

        ID3D12Resource* readback_resource = nullptr;
        const HRESULT readback_result = device->CreateCommittedResource(
            &heap_properties,
            D3D12_HEAP_FLAG_NONE,
            &readback_desc,
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS(&readback_resource));
        if (FAILED(readback_result) || !readback_resource)
        {
            release_native_resources();
            m_initialization_result = GpuTimingResult::ResolveFailed;
            return m_initialization_result;
        }
        m_readback_resource = readback_resource;
        dx12_set_debug_name(readback_resource, "DX12 Machine-readable GPU Timing Readback");

        const D3D12_RANGE persistent_read_range{
            0u,
            static_cast<SIZE_T>(readback_desc.Width) };
        void* mapped_readback = nullptr;
        const HRESULT map_result =
            readback_resource->Map(0u, &persistent_read_range, &mapped_readback);
        if (FAILED(map_result) || !mapped_readback)
        {
            release_native_resources();
            m_initialization_result = GpuTimingResult::ResolveFailed;
            return m_initialization_result;
        }
        m_mapped_readback = mapped_readback;

        m_initialization_result = GpuTimingResult::Success;
        return m_initialization_result;
    }

    auto DX12GpuTiming::shutdown() -> void
    {
        release_native_resources();
        m_tracker = {};
        m_slots = {};
        m_timestamp_frequency = 0;
        m_active_slot_index = kGpuTimingFrameSlotCount;
        m_initialization_result = GpuTimingResult::Unsupported;
    }

    auto DX12GpuTiming::begin_frame(CommandBuffer* cmd, uint64_t submitted_frame_index) -> GpuTimingResult
    {
        if (m_initialization_result != GpuTimingResult::Success)
        {
            return m_initialization_result;
        }
        const GpuTimingResult sticky_failure = m_tracker.sticky_failure();
        if (sticky_failure != GpuTimingResult::Success)
        {
            return sticky_failure;
        }
        if (!cmd || !cmd->get_native_handle())
        {
            return GpuTimingResult::InvalidState;
        }
        if (active_slot())
        {
            return fail_frame_recording(GpuTimingResult::InvalidState);
        }

        GpuTimingFrameHandle handle{};
        const GpuTimingResult begin_result = m_tracker.begin_recording(submitted_frame_index, handle);
        if (begin_result != GpuTimingResult::Success)
        {
            return begin_result;
        }

        SlotRuntime& slot = m_slots[handle.slot_index];
        slot = {};
        slot.handle = handle;
        slot.command_buffer = cmd->get_native_handle();
        slot.submitted_frame_index = submitted_frame_index;
        slot.recording = true;
        m_active_slot_index = handle.slot_index;

        const uint32_t query_base = handle.slot_index * kDX12GpuTimingQueriesPerSlot;
        static_cast<ID3D12GraphicsCommandList*>(slot.command_buffer)->EndQuery(
            static_cast<ID3D12QueryHeap*>(m_query_heap),
            D3D12_QUERY_TYPE_TIMESTAMP,
            query_base);
        return GpuTimingResult::Success;
    }

    auto DX12GpuTiming::begin_scope(
        CommandBuffer* cmd,
        uint64_t stable_name_hash,
        GpuTimingScopeHandle& out_handle) -> GpuTimingResult
    {
        SlotRuntime* slot = active_slot();
        if (!slot || !cmd || cmd->get_native_handle() != slot->command_buffer || slot->frame_ended)
        {
            return GpuTimingResult::InvalidState;
        }
        if (slot->scope_count == kMaxGpuTimingScopes)
        {
            slot->overflowed = true;
            return fail_frame_recording(GpuTimingResult::CapacityExceeded);
        }

        const uint32_t scope_index = slot->scope_count++;
        ScopeRuntime& scope = slot->scopes[scope_index];
        scope.stable_name_hash = stable_name_hash;
        scope.state = ScopeState::Recording;
        out_handle = { slot->handle.slot_index, scope_index, slot->handle.generation };

        const uint32_t query_base = slot->handle.slot_index * kDX12GpuTimingQueriesPerSlot;
        static_cast<ID3D12GraphicsCommandList*>(slot->command_buffer)->EndQuery(
            static_cast<ID3D12QueryHeap*>(m_query_heap),
            D3D12_QUERY_TYPE_TIMESTAMP,
            query_base + 2u + scope_index * 2u);
        return GpuTimingResult::Success;
    }

    auto DX12GpuTiming::end_scope(CommandBuffer* cmd, const GpuTimingScopeHandle& handle) -> GpuTimingResult
    {
        SlotRuntime* slot = active_slot();
        if (!slot || !cmd ||
            cmd->get_native_handle() != slot->command_buffer ||
            handle.frame_slot != slot->handle.slot_index ||
            handle.generation != slot->handle.generation ||
            handle.scope_slot >= slot->scope_count)
        {
            return GpuTimingResult::StaleHandle;
        }

        ScopeRuntime& scope = slot->scopes[handle.scope_slot];
        if (scope.state != ScopeState::Recording)
        {
            return fail_frame_recording(GpuTimingResult::InvalidState);
        }

        const uint32_t query_base = slot->handle.slot_index * kDX12GpuTimingQueriesPerSlot;
        static_cast<ID3D12GraphicsCommandList*>(slot->command_buffer)->EndQuery(
            static_cast<ID3D12QueryHeap*>(m_query_heap),
            D3D12_QUERY_TYPE_TIMESTAMP,
            query_base + 3u + handle.scope_slot * 2u);
        scope.state = ScopeState::Ended;
        return GpuTimingResult::Success;
    }

    auto DX12GpuTiming::end_frame(CommandBuffer* cmd) -> GpuTimingResult
    {
        SlotRuntime* slot = active_slot();
        if (!slot || !cmd || cmd->get_native_handle() != slot->command_buffer || slot->frame_ended)
        {
            return GpuTimingResult::InvalidState;
        }
        for (uint32_t scope_index = 0; scope_index < slot->scope_count; ++scope_index)
        {
            if (slot->scopes[scope_index].state != ScopeState::Ended)
            {
                return fail_frame_recording(GpuTimingResult::InvalidState);
            }
        }

        const uint32_t query_base = slot->handle.slot_index * kDX12GpuTimingQueriesPerSlot;
        auto* command_list = static_cast<ID3D12GraphicsCommandList*>(slot->command_buffer);
        command_list->EndQuery(
            static_cast<ID3D12QueryHeap*>(m_query_heap),
            D3D12_QUERY_TYPE_TIMESTAMP,
            query_base + 1u);
        const uint32_t query_count = 2u + slot->scope_count * 2u;
        const GpuTimingResult count_result =
            m_tracker.set_query_count(slot->handle, query_count);
        if (count_result != GpuTimingResult::Success)
        {
            return fail_frame_recording(count_result);
        }
        command_list->ResolveQueryData(
            static_cast<ID3D12QueryHeap*>(m_query_heap),
            D3D12_QUERY_TYPE_TIMESTAMP,
            query_base,
            query_count,
            static_cast<ID3D12Resource*>(m_readback_resource),
            static_cast<uint64_t>(query_base) * sizeof(uint64_t));
        slot->frame_ended = true;
        return GpuTimingResult::Success;
    }

    auto DX12GpuTiming::try_collect(GpuTimingFrameSnapshot& out_snapshot) -> GpuTimingResult
    {
        if (m_initialization_result != GpuTimingResult::Success)
        {
            return m_initialization_result;
        }
        const GpuTimingResult sticky_failure = m_tracker.sticky_failure();
        if (sticky_failure != GpuTimingResult::Success)
        {
            return sticky_failure;
        }

        const GpuTimingResult publish_result = m_tracker.try_publish(out_snapshot);
        if (publish_result == GpuTimingResult::Success)
        {
            return publish_result;
        }
        if (publish_result != GpuTimingResult::Pending)
        {
            return publish_result;
        }

        const GpuTimingResult resolve_result = poll_completed_slots();
        if (resolve_result != GpuTimingResult::Success && resolve_result != GpuTimingResult::Pending)
        {
            return resolve_result;
        }
        const GpuTimingResult post_resolve_failure = m_tracker.sticky_failure();
        if (post_resolve_failure != GpuTimingResult::Success)
        {
            return post_resolve_failure;
        }
        return m_tracker.try_publish(out_snapshot);
    }

    auto DX12GpuTiming::mark_frame_submitted(
        uint64_t frame_completion_value,
        void* completion_fence,
        bool command_buffer_submitted) -> GpuTimingResult
    {
        SlotRuntime* slot = active_slot();
        if (!slot)
        {
            return GpuTimingResult::Success;
        }
        if (!command_buffer_submitted)
        {
            return GpuTimingResult::Success;
        }
        if (!completion_fence || !slot->frame_ended)
        {
            return fail_frame_recording(GpuTimingResult::RecordFailed);
        }

        const GpuTimingResult submit_result =
            m_tracker.mark_submitted(slot->handle, frame_completion_value);
        if (submit_result != GpuTimingResult::Success)
        {
            return fail_frame_recording(submit_result);
        }

        slot->completion_value = frame_completion_value;
        slot->completion_fence = completion_fence;
        slot->recording = false;
        slot->submitted = true;
        m_active_slot_index = kGpuTimingFrameSlotCount;
        return GpuTimingResult::Success;
    }

    auto DX12GpuTiming::fail_frame_recording(GpuTimingResult failure) -> GpuTimingResult
    {
        if (failure == GpuTimingResult::Success || failure == GpuTimingResult::Pending)
        {
            return GpuTimingResult::InvalidState;
        }
        if (!active_slot())
        {
            return GpuTimingResult::Success;
        }
        return fail_slot(m_active_slot_index, failure);
    }

    auto DX12GpuTiming::cancel_unsubmitted_frame() -> GpuTimingResult
    {
        SlotRuntime* slot = active_slot();
        if (!slot)
        {
            return GpuTimingResult::Success;
        }
        const uint32_t slot_index = m_active_slot_index;
        const GpuTimingResult cancel_result = m_tracker.cancel_recording(slot->handle);
        clear_slot(slot_index);
        return cancel_result;
    }

    auto DX12GpuTiming::active_command_buffer() const -> void*
    {
        const SlotRuntime* slot = active_slot();
        return slot ? slot->command_buffer : nullptr;
    }

    auto DX12GpuTiming::release_native_resources() -> void
    {
        if (m_readback_resource)
        {
            auto* readback_resource = static_cast<ID3D12Resource*>(m_readback_resource);
            if (m_mapped_readback)
            {
                const D3D12_RANGE no_writes{ 0u, 0u };
                readback_resource->Unmap(0u, &no_writes);
            }
            readback_resource->Release();
        }
        if (m_query_heap)
        {
            static_cast<ID3D12QueryHeap*>(m_query_heap)->Release();
        }
        m_mapped_readback = nullptr;
        m_readback_resource = nullptr;
        m_query_heap = nullptr;
    }

    auto DX12GpuTiming::fail_slot(uint32_t slot_index, GpuTimingResult failure) -> GpuTimingResult
    {
        if (slot_index >= kGpuTimingFrameSlotCount)
        {
            return GpuTimingResult::InvalidState;
        }
        const GpuTimingResult fail_result = m_tracker.fail(m_slots[slot_index].handle, failure);
        clear_slot(slot_index);
        return fail_result == GpuTimingResult::Success ? failure : fail_result;
    }

    auto DX12GpuTiming::clear_slot(uint32_t slot_index) -> void
    {
        if (slot_index >= kGpuTimingFrameSlotCount)
        {
            return;
        }
        m_slots[slot_index] = {};
        if (m_active_slot_index == slot_index)
        {
            m_active_slot_index = kGpuTimingFrameSlotCount;
        }
    }

    auto DX12GpuTiming::find_oldest_submitted_slot() const -> uint32_t
    {
        uint32_t oldest_slot = kGpuTimingFrameSlotCount;
        uint64_t oldest_frame_index = 0;
        for (uint32_t slot_index = 0; slot_index < kGpuTimingFrameSlotCount; ++slot_index)
        {
            const SlotRuntime& slot = m_slots[slot_index];
            if (!slot.submitted)
            {
                continue;
            }
            if (oldest_slot == kGpuTimingFrameSlotCount ||
                slot.submitted_frame_index < oldest_frame_index)
            {
                oldest_slot = slot_index;
                oldest_frame_index = slot.submitted_frame_index;
            }
        }
        return oldest_slot;
    }

    auto DX12GpuTiming::materialize_slot(uint32_t slot_index) -> GpuTimingResult
    {
        SlotRuntime& slot = m_slots[slot_index];
        GpuTimingSubmissionInfo submission{};
        const GpuTimingResult submission_result =
            m_tracker.get_submission(slot.handle, submission);
        if (submission_result != GpuTimingResult::Success ||
            submission.query_count != 2u + slot.scope_count * 2u ||
            submission.query_count > kDX12GpuTimingQueriesPerSlot ||
            !m_mapped_readback ||
            dx12_validate_timestamp_frequency(m_timestamp_frequency) != GpuTimingResult::Success)
        {
            return fail_slot(slot_index, GpuTimingResult::ResolveFailed);
        }

        const uint32_t query_base = slot_index * kDX12GpuTimingQueriesPerSlot;
        const auto* timestamps = static_cast<const uint64_t*>(m_mapped_readback) + query_base;
        const auto elapsed_ms = [this](uint64_t begin_timestamp, uint64_t end_timestamp)
        {
            return dx12_timestamp_ms(begin_timestamp, end_timestamp, m_timestamp_frequency);
        };

        GpuTimingFrameSnapshot snapshot{};
        snapshot.submitted_frame_index = submission.submitted_frame_index;
        snapshot.frame_elapsed_ms = elapsed_ms(timestamps[0], timestamps[1]);
        snapshot.scope_count = slot.scope_count;
        snapshot.overflowed = slot.overflowed;
        if (!std::isfinite(snapshot.frame_elapsed_ms))
        {
            return fail_slot(slot_index, GpuTimingResult::ResolveFailed);
        }
        for (uint32_t scope_index = 0; scope_index < slot.scope_count; ++scope_index)
        {
            const uint32_t query_index = 2u + scope_index * 2u;
            const double scope_elapsed_ms =
                elapsed_ms(timestamps[query_index], timestamps[query_index + 1u]);
            if (!std::isfinite(scope_elapsed_ms))
            {
                return fail_slot(slot_index, GpuTimingResult::ResolveFailed);
            }
            snapshot.scopes[scope_index] = {
                slot.scopes[scope_index].stable_name_hash,
                scope_elapsed_ms };
        }

        const GpuTimingResult complete_result = m_tracker.mark_completed(slot.handle, snapshot);
        clear_slot(slot_index);
        return complete_result;
    }

    auto DX12GpuTiming::poll_completed_slots() -> GpuTimingResult
    {
        while (true)
        {
            const uint32_t slot_index = find_oldest_submitted_slot();
            if (slot_index == kGpuTimingFrameSlotCount)
            {
                return GpuTimingResult::Success;
            }

            const SlotRuntime& slot = m_slots[slot_index];
            if (!slot.completion_fence)
            {
                return fail_slot(slot_index, GpuTimingResult::ResolveFailed);
            }
            const uint64_t completed_value =
                static_cast<ID3D12Fence*>(slot.completion_fence)->GetCompletedValue();
            if (completed_value == UINT64_MAX)
            {
                return fail_slot(slot_index, GpuTimingResult::DeviceLost);
            }
            if (completed_value < slot.completion_value)
            {
                return GpuTimingResult::Pending;
            }

            const GpuTimingResult materialize_result = materialize_slot(slot_index);
            if (materialize_result != GpuTimingResult::Success)
            {
                return materialize_result;
            }
        }
    }

    auto DX12GpuTiming::active_slot() -> SlotRuntime*
    {
        return m_active_slot_index < kGpuTimingFrameSlotCount
            ? &m_slots[m_active_slot_index]
            : nullptr;
    }

    auto DX12GpuTiming::active_slot() const -> const SlotRuntime*
    {
        return m_active_slot_index < kGpuTimingFrameSlotCount
            ? &m_slots[m_active_slot_index]
            : nullptr;
    }
}
