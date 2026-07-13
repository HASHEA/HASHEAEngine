#include "VulkanGpuTiming.h"

#include "Graphics/CommandBuffer.h"
#include "VulkanWrapper.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace RHI
{
    auto VulkanGpuTiming::init(
        void* physical_device_handle,
        void* device_handle,
        uint32_t graphics_queue_family,
        void* graphics_timeline_semaphore,
        bool synchronization2_enabled) -> GpuTimingResult
    {
        const VkPhysicalDevice physical_device = static_cast<VkPhysicalDevice>(physical_device_handle);
        const VkDevice device = static_cast<VkDevice>(device_handle);
        m_device = device_handle;
        m_graphics_timeline_semaphore = graphics_timeline_semaphore;
        m_initialization_result = GpuTimingResult::Unsupported;
        if (!physical_device || !device)
        {
            return m_initialization_result;
        }

        uint32_t queue_family_count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &queue_family_count, nullptr);
        if (graphics_queue_family >= queue_family_count)
        {
            return m_initialization_result;
        }
        std::vector<VkQueueFamilyProperties> queue_family_properties(queue_family_count);
        vkGetPhysicalDeviceQueueFamilyProperties(
            physical_device,
            &queue_family_count,
            queue_family_properties.data());

        VkPhysicalDeviceProperties device_properties{};
        vkGetPhysicalDeviceProperties(physical_device, &device_properties);
        m_timestamp_valid_bits = queue_family_properties[graphics_queue_family].timestampValidBits;
        m_timestamp_period_ns = static_cast<double>(device_properties.limits.timestampPeriod);
        if (m_timestamp_valid_bits == 0u ||
            !(m_timestamp_period_ns > 0.0) ||
            !std::isfinite(m_timestamp_period_ns))
        {
            return m_initialization_result;
        }

        VkQueryPoolCreateInfo query_pool_info{ VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO };
        query_pool_info.queryType = VK_QUERY_TYPE_TIMESTAMP;
        query_pool_info.queryCount = kGpuTimingFrameSlotCount * kVulkanGpuTimingQueriesPerSlot;
        VkQueryPool query_pool = VK_NULL_HANDLE;
        const VkResult create_result = vkCreateQueryPool(device, &query_pool_info, nullptr, &query_pool);
        if (create_result != VK_SUCCESS)
        {
            m_query_pool = nullptr;
            m_initialization_result = GpuTimingResult::RecordFailed;
            return m_initialization_result;
        }
        m_query_pool = query_pool;

        m_use_synchronization2 = synchronization2_enabled && vkCmdWriteTimestamp2KHR != nullptr;
        m_initialization_result = GpuTimingResult::Success;
        return m_initialization_result;
    }

    auto VulkanGpuTiming::shutdown() -> void
    {
        if (m_query_pool && m_device)
        {
            vkDestroyQueryPool(
                static_cast<VkDevice>(m_device),
                static_cast<VkQueryPool>(m_query_pool),
                nullptr);
        }
        m_query_pool = nullptr;
        m_device = nullptr;
        m_graphics_timeline_semaphore = nullptr;
        m_active_slot_index = kGpuTimingFrameSlotCount;
        m_initialization_result = GpuTimingResult::Unsupported;
        for (SlotRuntime& slot : m_slots)
        {
            slot = {};
        }
    }

    auto VulkanGpuTiming::begin_frame(CommandBuffer* cmd, uint64_t submitted_frame_index) -> GpuTimingResult
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

        const uint32_t slot_index = handle.slot_index;
        SlotRuntime& slot = m_slots[slot_index];
        slot = {};
        slot.handle = handle;
        slot.command_buffer = cmd->get_native_handle();
        slot.submitted_frame_index = submitted_frame_index;
        slot.recording = true;
        m_active_slot_index = slot_index;

        const uint32_t query_base = slot_index * kVulkanGpuTimingQueriesPerSlot;
        vkCmdResetQueryPool(
            static_cast<VkCommandBuffer>(slot.command_buffer),
            static_cast<VkQueryPool>(m_query_pool),
            query_base,
            kVulkanGpuTimingQueriesPerSlot);
        write_timestamp(slot.command_buffer, query_base, false);
        return GpuTimingResult::Success;
    }

    auto VulkanGpuTiming::begin_scope(
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

        const uint32_t query_base = slot->handle.slot_index * kVulkanGpuTimingQueriesPerSlot;
        write_timestamp(slot->command_buffer, query_base + 2u + scope_index * 2u, false);
        return GpuTimingResult::Success;
    }

    auto VulkanGpuTiming::end_scope(CommandBuffer* cmd, const GpuTimingScopeHandle& handle) -> GpuTimingResult
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

        const uint32_t query_base = slot->handle.slot_index * kVulkanGpuTimingQueriesPerSlot;
        write_timestamp(slot->command_buffer, query_base + 3u + handle.scope_slot * 2u, true);
        scope.state = ScopeState::Ended;
        return GpuTimingResult::Success;
    }

    auto VulkanGpuTiming::end_frame(CommandBuffer* cmd) -> GpuTimingResult
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

        const uint32_t query_base = slot->handle.slot_index * kVulkanGpuTimingQueriesPerSlot;
        write_timestamp(slot->command_buffer, query_base + 1u, true);
        const GpuTimingResult count_result =
            m_tracker.set_query_count(slot->handle, 2u + slot->scope_count * 2u);
        if (count_result != GpuTimingResult::Success)
        {
            return fail_frame_recording(count_result);
        }
        slot->frame_ended = true;
        return GpuTimingResult::Success;
    }

    auto VulkanGpuTiming::try_collect(GpuTimingFrameSnapshot& out_snapshot) -> GpuTimingResult
    {
        if (m_initialization_result != GpuTimingResult::Success)
        {
            return m_initialization_result;
        }
        if (m_tracker.sticky_failure() != GpuTimingResult::Success)
        {
            return m_tracker.sticky_failure();
        }

        GpuTimingResult publish_result = m_tracker.try_publish(out_snapshot);
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
        if (m_tracker.sticky_failure() != GpuTimingResult::Success)
        {
            return m_tracker.sticky_failure();
        }
        return m_tracker.try_publish(out_snapshot);
    }

    auto VulkanGpuTiming::mark_frame_submitted(
        uint64_t frame_completion_value,
        void* completion_fence,
        void* const* submitted_command_buffers,
        uint32_t submitted_command_buffer_count) -> GpuTimingResult
    {
        SlotRuntime* slot = active_slot();
        if (!slot)
        {
            return GpuTimingResult::Success;
        }

        bool command_buffer_submitted = false;
        for (uint32_t command_index = 0; command_index < submitted_command_buffer_count; ++command_index)
        {
            if (submitted_command_buffers && submitted_command_buffers[command_index] == slot->command_buffer)
            {
                command_buffer_submitted = true;
                break;
            }
        }
        if (!command_buffer_submitted)
        {
            const GpuTimingResult cancel_result = m_tracker.cancel_recording(slot->handle);
            clear_slot(m_active_slot_index);
            return cancel_result;
        }
        if (!slot->frame_ended)
        {
            return fail_frame_recording(GpuTimingResult::RecordFailed);
        }

        const GpuTimingResult submit_result = m_tracker.mark_submitted(slot->handle, frame_completion_value);
        if (submit_result != GpuTimingResult::Success)
        {
            return fail_frame_recording(submit_result);
        }

        slot->completion_value = frame_completion_value;
        slot->completion_fence = completion_fence;
        slot->completion_kind = completion_fence == nullptr ? CompletionKind::Timeline : CompletionKind::Fence;
        slot->recording = false;
        slot->submitted = true;
        m_active_slot_index = kGpuTimingFrameSlotCount;
        return GpuTimingResult::Success;
    }

    auto VulkanGpuTiming::fail_frame_recording(GpuTimingResult failure) -> GpuTimingResult
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

    auto VulkanGpuTiming::resolve_timeline_completion(uint64_t completed_value) -> GpuTimingResult
    {
        return resolve_known_completion(CompletionKind::Timeline, completed_value, nullptr);
    }

    auto VulkanGpuTiming::resolve_fence_completion(void* completed_fence) -> GpuTimingResult
    {
        if (!completed_fence)
        {
            return GpuTimingResult::InvalidState;
        }
        return resolve_known_completion(CompletionKind::Fence, 0u, completed_fence);
    }

    auto VulkanGpuTiming::write_timestamp(
        void* command_buffer_handle,
        uint32_t query_index,
        bool end_point) const -> void
    {
        const VkCommandBuffer command_buffer = static_cast<VkCommandBuffer>(command_buffer_handle);
        const VkQueryPool query_pool = static_cast<VkQueryPool>(m_query_pool);
        if (m_use_synchronization2)
        {
            vkCmdWriteTimestamp2KHR(
                command_buffer,
                VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT_KHR,
                query_pool,
                query_index);
            return;
        }

        vkCmdWriteTimestamp(
            command_buffer,
            end_point ? VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            query_pool,
            query_index);
    }

    auto VulkanGpuTiming::fail_slot(uint32_t slot_index, GpuTimingResult failure) -> GpuTimingResult
    {
        if (slot_index >= kGpuTimingFrameSlotCount)
        {
            return GpuTimingResult::InvalidState;
        }
        const GpuTimingResult fail_result = m_tracker.fail(m_slots[slot_index].handle, failure);
        clear_slot(slot_index);
        return fail_result == GpuTimingResult::Success ? failure : fail_result;
    }

    auto VulkanGpuTiming::clear_slot(uint32_t slot_index) -> void
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

    auto VulkanGpuTiming::find_oldest_submitted_slot() const -> uint32_t
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
            if (oldest_slot == kGpuTimingFrameSlotCount || slot.submitted_frame_index < oldest_frame_index)
            {
                oldest_slot = slot_index;
                oldest_frame_index = slot.submitted_frame_index;
            }
        }
        return oldest_slot;
    }

    auto VulkanGpuTiming::materialize_slot(uint32_t slot_index) -> GpuTimingResult
    {
        SlotRuntime& slot = m_slots[slot_index];
        GpuTimingSubmissionInfo submission{};
        const GpuTimingResult submission_result = m_tracker.get_submission(slot.handle, submission);
        if (submission_result != GpuTimingResult::Success ||
            submission.query_count != 2u + slot.scope_count * 2u ||
            submission.query_count > kVulkanGpuTimingQueriesPerSlot)
        {
            return fail_slot(slot_index, GpuTimingResult::ResolveFailed);
        }

        std::array<QueryResult, kVulkanGpuTimingQueriesPerSlot> query_results{};
        const uint32_t query_base = slot_index * kVulkanGpuTimingQueriesPerSlot;
        const VkResult query_result = vkGetQueryPoolResults(
            static_cast<VkDevice>(m_device),
            static_cast<VkQueryPool>(m_query_pool),
            query_base,
            submission.query_count,
            sizeof(QueryResult) * submission.query_count,
            query_results.data(),
            sizeof(QueryResult),
            VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
        if (query_result != VK_SUCCESS)
        {
            return fail_slot(
                slot_index,
                query_result == VK_ERROR_DEVICE_LOST ? GpuTimingResult::DeviceLost : GpuTimingResult::ResolveFailed);
        }
        for (uint32_t query_index = 0; query_index < submission.query_count; ++query_index)
        {
            if (query_results[query_index].available == 0u)
            {
                return fail_slot(slot_index, GpuTimingResult::ResolveFailed);
            }
        }

        const auto elapsed_ms = [this](uint64_t begin_timestamp, uint64_t end_timestamp)
        {
            return static_cast<double>(vulkan_timestamp_delta(
                       begin_timestamp,
                       end_timestamp,
                       m_timestamp_valid_bits)) *
                m_timestamp_period_ns / 1'000'000.0;
        };

        GpuTimingFrameSnapshot snapshot{};
        snapshot.submitted_frame_index = submission.submitted_frame_index;
        snapshot.frame_elapsed_ms = elapsed_ms(query_results[0].timestamp, query_results[1].timestamp);
        snapshot.scope_count = slot.scope_count;
        snapshot.overflowed = slot.overflowed;
        for (uint32_t scope_index = 0; scope_index < slot.scope_count; ++scope_index)
        {
            const uint32_t query_index = 2u + scope_index * 2u;
            snapshot.scopes[scope_index] = {
                slot.scopes[scope_index].stable_name_hash,
                elapsed_ms(query_results[query_index].timestamp, query_results[query_index + 1u].timestamp) };
        }

        const GpuTimingResult complete_result = m_tracker.mark_completed(slot.handle, snapshot);
        clear_slot(slot_index);
        return complete_result;
    }

    auto VulkanGpuTiming::poll_completed_slots() -> GpuTimingResult
    {
        while (true)
        {
            const uint32_t slot_index = find_oldest_submitted_slot();
            if (slot_index == kGpuTimingFrameSlotCount)
            {
                return GpuTimingResult::Success;
            }

            const SlotRuntime& slot = m_slots[slot_index];
            if (slot.completion_kind == CompletionKind::Timeline)
            {
                if (!m_graphics_timeline_semaphore)
                {
                    return fail_slot(slot_index, GpuTimingResult::ResolveFailed);
                }
                uint64_t completed_value = 0;
                const VkResult counter_result =
                    vkGetSemaphoreCounterValue(
                        static_cast<VkDevice>(m_device),
                        static_cast<VkSemaphore>(m_graphics_timeline_semaphore),
                        &completed_value);
                if (counter_result != VK_SUCCESS)
                {
                    return fail_slot(
                        slot_index,
                        counter_result == VK_ERROR_DEVICE_LOST ? GpuTimingResult::DeviceLost : GpuTimingResult::ResolveFailed);
                }
                if (completed_value < slot.completion_value)
                {
                    return GpuTimingResult::Pending;
                }
            }
            else if (slot.completion_kind == CompletionKind::Fence)
            {
                const VkResult fence_result = vkGetFenceStatus(
                    static_cast<VkDevice>(m_device),
                    static_cast<VkFence>(slot.completion_fence));
                if (fence_result == VK_NOT_READY)
                {
                    return GpuTimingResult::Pending;
                }
                if (fence_result != VK_SUCCESS)
                {
                    return fail_slot(
                        slot_index,
                        fence_result == VK_ERROR_DEVICE_LOST ? GpuTimingResult::DeviceLost : GpuTimingResult::ResolveFailed);
                }
            }
            else
            {
                return fail_slot(slot_index, GpuTimingResult::InvalidState);
            }

            const GpuTimingResult materialize_result = materialize_slot(slot_index);
            if (materialize_result != GpuTimingResult::Success)
            {
                return materialize_result;
            }
        }
    }

    auto VulkanGpuTiming::resolve_known_completion(
        CompletionKind kind,
        uint64_t completed_value,
        void* completed_fence) -> GpuTimingResult
    {
        while (true)
        {
            const uint32_t slot_index = find_oldest_submitted_slot();
            if (slot_index == kGpuTimingFrameSlotCount)
            {
                return GpuTimingResult::Success;
            }
            const SlotRuntime& slot = m_slots[slot_index];
            if (slot.completion_kind != kind)
            {
                return GpuTimingResult::Success;
            }
            if (kind == CompletionKind::Timeline && slot.completion_value > completed_value)
            {
                return GpuTimingResult::Success;
            }
            if (kind == CompletionKind::Fence && slot.completion_fence != completed_fence)
            {
                return GpuTimingResult::Success;
            }

            const GpuTimingResult materialize_result = materialize_slot(slot_index);
            if (materialize_result != GpuTimingResult::Success)
            {
                return materialize_result;
            }
        }
    }

    auto VulkanGpuTiming::active_slot() -> SlotRuntime*
    {
        return m_active_slot_index < kGpuTimingFrameSlotCount ? &m_slots[m_active_slot_index] : nullptr;
    }

    auto VulkanGpuTiming::active_slot() const -> const SlotRuntime*
    {
        return m_active_slot_index < kGpuTimingFrameSlotCount ? &m_slots[m_active_slot_index] : nullptr;
    }
}
