#pragma once
#include "Base/hcore.h"
#include "DX12Wrapper.h"
#include <atomic>
#include <cstdint>

namespace RHI
{
	struct DX12FenceSignalResult
	{
		HRESULT hresult = E_FAIL;
		uint64_t target_value = 0u;

		bool succeeded() const
		{
			return SUCCEEDED(hresult) && target_value != 0u;
		}
	};

	enum class DX12FenceWaitStatus : uint8_t
	{
		Completed,
		Timeout,
		DeviceRemoved,
		EventRegistrationFailed,
		WaitFailed,
		InvalidFence,
	};

	struct DX12FenceWaitResult
	{
		DX12FenceWaitStatus status = DX12FenceWaitStatus::InvalidFence;
		uint64_t completed_value = 0u;
		uint64_t target_value = 0u;
		HRESULT event_registration_hresult = E_FAIL;
		DWORD wait_result = WAIT_FAILED;
		DWORD wait_error = ERROR_SUCCESS;
		bool event_registration_attempted = false;
		bool wait_attempted = false;

		bool completed() const
		{
			return status == DX12FenceWaitStatus::Completed;
		}

		bool should_continue_after_wake() const
		{
			return wait_attempted &&
				wait_result == WAIT_OBJECT_0 &&
				completed_value != UINT64_MAX &&
				completed_value < target_value;
		}
	};

	enum class DX12GraphicsCompletionState : uint8_t
	{
		Trackable,
		Lost,
	};

	enum class DX12GraphicsTeardownReadiness : uint8_t
	{
		Unknown,
		Drained,
		DeviceRemoved,
	};

	ASH_API DX12GraphicsCompletionState advance_dx12_graphics_completion_state(
		DX12GraphicsCompletionState current_state,
		bool batch_executed,
		const DX12FenceSignalResult& signal_result);
	ASH_API DX12GraphicsTeardownReadiness classify_dx12_graphics_teardown_readiness(
		const DX12FenceSignalResult& tail_signal_result,
		uint64_t completed_value,
		HRESULT device_removed_reason);
	// Classifies terminal outcomes only. Callers must first continue waiting when
	// DX12FenceWaitResult::should_continue_after_wake() reports a stale wake.
	ASH_API DX12FenceWaitStatus classify_dx12_fence_wait_status(
		uint64_t completed_value,
		uint64_t target_value,
		bool event_registration_attempted,
		HRESULT event_registration_hresult,
		bool wait_attempted,
		DWORD wait_result);

	// Owns the monotonic safety decision for reusing resources protected by the
	// graphics queue fence. Once an executed batch loses its completion marker,
	// only full context shutdown may reset this policy.
	class DX12GraphicsCompletionPolicy
	{
	public:
		ASH_API void observe_submission(
			bool batch_executed,
			const DX12FenceSignalResult& signal_result);
		ASH_API void observe_queue_work();
		ASH_API void observe_required_completion_signal(
			const DX12FenceSignalResult& signal_result);
		ASH_API void record_teardown_readiness(DX12GraphicsTeardownReadiness readiness);
		ASH_API void mark_completion_lost();

		ASH_API bool can_issue_work() const;
		ASH_API bool can_reuse_frame_resources() const;
		ASH_API bool can_report_completion() const;
		ASH_API bool is_lost() const;
		ASH_API DX12GraphicsTeardownReadiness cached_teardown_readiness() const;
		ASH_API void reset_after_shutdown();

	private:
		std::atomic<DX12GraphicsCompletionState> m_state{
			DX12GraphicsCompletionState::Trackable};
		std::atomic<DX12GraphicsTeardownReadiness> m_teardown_readiness{
			DX12GraphicsTeardownReadiness::Drained};
	};

	class DX12Fence
	{
	public:
		DX12Fence() = default;
		~DX12Fence();

		bool init(ID3D12Device* device, uint64_t initialValue = 0);
		void shutdown();

		DX12FenceSignalResult signal(ID3D12CommandQueue* queue);
		DX12FenceWaitResult wait(uint64_t timeout = INFINITE);
		void cpu_signal(uint64_t value);
		uint64_t get_completed_value() const;
		uint64_t get_current_value() const { return m_fenceValue; }
		ID3D12Fence* get_fence() const { return m_fence.Get(); }

	private:
		ComPtr<ID3D12Fence> m_fence;
		HANDLE m_fenceEvent = nullptr;
		uint64_t m_fenceValue = 0;
	};
}
