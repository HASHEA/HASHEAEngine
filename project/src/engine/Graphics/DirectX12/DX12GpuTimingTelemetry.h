#pragma once

#include "DX12Fence.h"
#include "Graphics/GpuTimingTelemetryRHI.h"

#include <array>
#include <cstdint>

namespace RHI
{
	struct DX12GpuTimingResolveRange
	{
		uint32_t start_query = 0u;
		uint32_t query_count = 0u;
		uint64_t destination_offset_bytes = 0u;
	};

	enum class DX12GpuTimingResolveResult : uint8_t
	{
		Ready,
		Unsupported,
	};

	enum class DX12GpuTimingFencePollResult : uint8_t
	{
		Ready,
		Pending,
		DeviceRemoved,
		InvalidTarget,
	};

	enum class DX12GpuTimingSubmitResult : uint8_t
	{
		Accepted,
		CommandListNotExecuted,
		FenceSignalFailed,
		InvalidFenceTarget,
	};

	enum class DX12GpuTimingSafetyState : uint8_t
	{
		Operational,
		Quarantined,
	};

	ASH_API bool make_dx12_gpu_timing_resolve_range(
		uint32_t physical_slot,
		uint32_t query_count,
		DX12GpuTimingResolveRange& out_range);
	ASH_API DX12GpuTimingResolveResult resolve_dx12_gpu_timing_record(
		const GpuTimingFrameRecord& record,
		const std::array<uint64_t, kGpuTimingQueriesPerFrame>& timestamps,
		uint64_t timestamp_frequency_hz,
		GpuFrameTimingSample& out_sample);
	ASH_API DX12GpuTimingFencePollResult classify_dx12_gpu_timing_fence(
		uint64_t completed_value,
		uint64_t target_value);
	ASH_API DX12GpuTimingSubmitResult classify_dx12_gpu_timing_submit(
		ID3D12CommandList* expected_command_list,
		ID3D12CommandList* const* executed_command_lists,
		uint32_t executed_command_list_count,
		const DX12FenceSignalResult& signal_result);
	ASH_API DX12GpuTimingSafetyState advance_dx12_gpu_timing_safety_state(
		DX12GpuTimingSafetyState current_state,
		DX12GpuTimingSubmitResult submit_result);
	ASH_API DX12GpuTimingSafetyState
	advance_dx12_gpu_timing_safety_state_after_abort(
		DX12GpuTimingSafetyState current_state,
		bool submission_bound);
	ASH_API uint32_t terminate_dx12_gpu_timing_tracking(
		GpuTimingFrameState& frame_state,
		std::array<uint64_t, kGpuTimingFrameRingDepth>& slot_frame_ids,
		std::array<uint64_t, kGpuTimingFrameRingDepth>& slot_fence_targets,
		std::array<bool, kGpuTimingFrameRingDepth>& slot_has_frame,
		GpuTimingInvalidReason reason,
		std::array<GpuFrameTimingSample, kGpuTimingFrameRingDepth>& out_samples);

	class DX12GpuTimingTelemetry final : public IGpuTimingTelemetry
	{
	public:
		DX12GpuTimingTelemetry() = default;
		~DX12GpuTimingTelemetry() override;

		DX12GpuTimingTelemetry(const DX12GpuTimingTelemetry&) = delete;
		DX12GpuTimingTelemetry& operator=(const DX12GpuTimingTelemetry&) = delete;

		bool init(
			ID3D12Device* device,
			ID3D12CommandQueue* graphics_queue,
			IDXGIAdapter1* adapter);
		void shutdown();
		bool resolve_recycled_slot(uint32_t physical_slot, uint64_t completed_fence_value);
		void observe_submission(
			ID3D12CommandList* const* executed_command_lists,
			uint32_t executed_command_list_count,
			const DX12FenceSignalResult& signal_result);
		void notify_terminal_completion(GpuTimingInvalidReason reason);

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
		bool release_recycled_slot(
			uint32_t physical_slot,
			const GpuTimingFrameRecord& record,
			const GpuFrameTimingSample* completed_sample);

		ComPtr<ID3D12QueryHeap> m_query_heap;
		ComPtr<ID3D12Resource> m_readback_resource;
		uint64_t* m_mapped_timestamps = nullptr;
		uint64_t m_timestamp_frequency_hz = 0u;
		GpuTimingTelemetryInfo m_info{};
		GpuTimingFrameState m_frame_state{};
		std::array<GpuFrameTimingSample, kGpuTimingFrameRingDepth> m_completed_samples{};
		std::array<uint64_t, kGpuTimingFrameRingDepth> m_slot_frame_ids{};
		std::array<uint64_t, kGpuTimingFrameRingDepth> m_slot_fence_targets{};
		std::array<bool, kGpuTimingFrameRingDepth> m_slot_has_frame{};
		uint32_t m_completed_head = 0u;
		uint32_t m_completed_count = 0u;
		uint32_t m_available_slot = kGpuTimingFrameRingDepth;
		uint32_t m_active_slot = kGpuTimingFrameRingDepth;
		uint32_t m_active_query_count = 0u;
		uint64_t m_active_frame_id = 0u;
		uint64_t m_active_fence_target = 0u;
		CommandBuffer* m_active_command_buffer = nullptr;
		ID3D12GraphicsCommandList* m_active_native_command_list = nullptr;
		bool m_frame_ended = false;
		bool m_submission_bound = false;
		bool m_supported = false;
		bool m_device_removed = false;
		DX12GpuTimingSafetyState m_safety_state =
			DX12GpuTimingSafetyState::Operational;
	};
}
