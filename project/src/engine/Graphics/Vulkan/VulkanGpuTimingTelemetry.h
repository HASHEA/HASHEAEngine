#pragma once

#include "Graphics/GpuTimingTelemetryRHI.h"
#include "volk/volk.h"

#include <array>
#include <cstdint>

namespace RHI
{
	struct VulkanGpuTimingRawQueryResult
	{
		uint64_t timestamp = 0u;
		uint64_t availability = 0u;
	};

	enum class VulkanGpuTimingResolveResult : uint8_t
	{
		Ready,
		Pending,
		Unsupported,
	};

	enum class VulkanGpuTimingSubmitResult : uint8_t
	{
		Accepted,
		CommandBufferNotExecuted,
		QueueSubmitFailed,
		CompletionBindingMissing,
	};

	enum class VulkanGpuTimingSafetyState : uint8_t
	{
		Operational,
		Quarantined,
	};

	ASH_API VulkanGpuTimingResolveResult resolve_vulkan_gpu_timing_record(
		const GpuTimingFrameRecord& record,
		const std::array<VulkanGpuTimingRawQueryResult, kGpuTimingQueriesPerFrame>& raw_results,
		uint32_t timestamp_valid_bits,
		double timestamp_period_ns,
		GpuFrameTimingSample& out_sample);
	ASH_API VulkanGpuTimingSubmitResult classify_vulkan_gpu_timing_submit(
		VkCommandBuffer expected_command_buffer,
		const VkCommandBuffer* executed_command_buffers,
		uint32_t executed_command_buffer_count,
		VkResult queue_submit_result,
		bool completion_binding_established);
	ASH_API VulkanGpuTimingSafetyState advance_vulkan_gpu_timing_safety_state(
		VulkanGpuTimingSafetyState current_state,
		VulkanGpuTimingSubmitResult submit_result);
	ASH_API VulkanGpuTimingSafetyState
	advance_vulkan_gpu_timing_safety_state_after_abort(
		VulkanGpuTimingSafetyState current_state,
		bool submission_bound);
	ASH_API bool vulkan_gpu_timing_slots_reusable(
		VulkanGpuTimingSafetyState safety_state);

	class VulkanGpuTimingTelemetry final : public IGpuTimingTelemetry
	{
	public:
		VulkanGpuTimingTelemetry() = default;
		~VulkanGpuTimingTelemetry() override;

		VulkanGpuTimingTelemetry(const VulkanGpuTimingTelemetry&) = delete;
		VulkanGpuTimingTelemetry& operator=(const VulkanGpuTimingTelemetry&) = delete;

		bool init(
			VkPhysicalDevice physical_device,
			VkDevice device,
			uint32_t graphics_queue_family,
			VkAllocationCallbacks* allocation_callbacks);
		void shutdown();
		bool resolve_recycled_slot(uint32_t physical_slot, bool completion_observed);
		void observe_submission(
			const VkCommandBuffer* executed_command_buffers,
			uint32_t executed_command_buffer_count,
			VkResult queue_submit_result,
			bool completion_binding_established);

		bool begin_frame(CommandBuffer* cmd, uint64_t frame_id) override;
		bool begin_scope(CommandBuffer* cmd, GpuTimingMetric metric) override;
		void end_scope(CommandBuffer* cmd, GpuTimingMetric metric) override;
		void end_frame(CommandBuffer* cmd, uint64_t frame_id) override;
		bool commit_frame(uint64_t frame_id) override;
		void abort_frame(uint64_t frame_id, GpuTimingInvalidReason reason) override;
		GpuTimingPollResult poll_completed_frame(GpuFrameTimingSample& out_sample) override;
		GpuTimingTelemetryInfo get_info() const override;

	private:
		bool enqueue_completed_sample(const GpuFrameTimingSample& sample);
		bool dequeue_completed_sample(GpuFrameTimingSample& out_sample);
		void clear_active_frame();

		VkDevice m_device = VK_NULL_HANDLE;
		VkAllocationCallbacks* m_allocation_callbacks = nullptr;
		VkQueryPool m_query_pool = VK_NULL_HANDLE;
		GpuTimingTelemetryInfo m_info{};
		GpuTimingFrameState m_frame_state{};
		std::array<GpuFrameTimingSample, kGpuTimingFrameRingDepth> m_completed_samples{};
		std::array<uint64_t, kGpuTimingFrameRingDepth> m_slot_frame_ids{};
		std::array<bool, kGpuTimingFrameRingDepth> m_slot_has_frame{};
		uint32_t m_completed_head = 0u;
		uint32_t m_completed_count = 0u;
		uint32_t m_available_slot = kGpuTimingFrameRingDepth;
		uint32_t m_active_slot = kGpuTimingFrameRingDepth;
		uint64_t m_active_frame_id = 0u;
		CommandBuffer* m_active_command_buffer = nullptr;
		VkCommandBuffer m_active_native_command_buffer = VK_NULL_HANDLE;
		bool m_frame_ended = false;
		bool m_submission_bound = false;
		bool m_supported = false;
		VulkanGpuTimingSafetyState m_safety_state =
			VulkanGpuTimingSafetyState::Operational;
	};
}
