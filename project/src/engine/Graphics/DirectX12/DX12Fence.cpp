#include "DX12Fence.h"
#include "Base/hlog.h"
#include "Base/hassert.h"

namespace RHI
{
	DX12GraphicsCompletionState advance_dx12_graphics_completion_state(
		DX12GraphicsCompletionState current_state,
		bool batch_executed,
		const DX12FenceSignalResult& signal_result)
	{
		if (current_state == DX12GraphicsCompletionState::Lost || !batch_executed)
		{
			return current_state;
		}
		return signal_result.succeeded()
			? DX12GraphicsCompletionState::Trackable
			: DX12GraphicsCompletionState::Lost;
	}

	DX12GraphicsTeardownReadiness classify_dx12_graphics_teardown_readiness(
		const DX12FenceSignalResult& tail_signal_result,
		uint64_t completed_value,
		HRESULT device_removed_reason)
	{
		if (device_removed_reason != S_OK)
		{
			return DX12GraphicsTeardownReadiness::DeviceRemoved;
		}
		if (tail_signal_result.succeeded() &&
			completed_value != UINT64_MAX &&
			completed_value >= tail_signal_result.target_value)
		{
			return DX12GraphicsTeardownReadiness::Drained;
		}
		return DX12GraphicsTeardownReadiness::Unknown;
	}

	DX12FenceWaitStatus classify_dx12_fence_wait_status(
		uint64_t completed_value,
		uint64_t target_value,
		bool event_registration_attempted,
		HRESULT event_registration_hresult,
		bool wait_attempted,
		DWORD wait_result)
	{
		if (completed_value == UINT64_MAX)
		{
			return DX12FenceWaitStatus::DeviceRemoved;
		}
		if (completed_value >= target_value)
		{
			return DX12FenceWaitStatus::Completed;
		}
		if (event_registration_attempted && FAILED(event_registration_hresult))
		{
			return DX12FenceWaitStatus::EventRegistrationFailed;
		}
		if (wait_attempted && wait_result == WAIT_TIMEOUT)
		{
			return DX12FenceWaitStatus::Timeout;
		}
		return DX12FenceWaitStatus::WaitFailed;
	}

	void DX12GraphicsCompletionPolicy::observe_submission(
		bool batch_executed,
		const DX12FenceSignalResult& signal_result)
	{
		// Queue::Signal is ordered queue work even when no command-list batch was
		// executed. A failed empty submission did not enqueue work and therefore
		// must not erase a previously proven teardown state.
		if (batch_executed || signal_result.succeeded())
		{
			observe_queue_work();
		}
		const DX12GraphicsCompletionState current_state =
			m_state.load(std::memory_order_acquire);
		const DX12GraphicsCompletionState next_state =
			advance_dx12_graphics_completion_state(
			current_state,
			batch_executed,
			signal_result);
		if (next_state == DX12GraphicsCompletionState::Lost)
		{
			m_state.store(next_state, std::memory_order_release);
		}
	}

	void DX12GraphicsCompletionPolicy::observe_queue_work()
	{
		m_teardown_readiness.store(
			DX12GraphicsTeardownReadiness::Unknown,
			std::memory_order_release);
	}

	void DX12GraphicsCompletionPolicy::observe_required_completion_signal(
		const DX12FenceSignalResult& signal_result)
	{
		if (signal_result.succeeded())
		{
			// The successful queue-tail Signal is new queue work even when a concurrent
			// proof just published Drained. Always invalidate that cache until this
			// target is observed, while preserving an already-Lost completion state.
			observe_submission(false, signal_result);
			return;
		}
		if (m_teardown_readiness.load(std::memory_order_acquire) !=
			DX12GraphicsTeardownReadiness::Unknown)
		{
			return;
		}
		// The failed marker proves no new queue work was issued. Poison completion,
		// but do not overwrite a terminal readiness that another lifecycle reader
		// may have published after the acquire load above.
		mark_completion_lost();
	}

	void DX12GraphicsCompletionPolicy::record_teardown_readiness(
		DX12GraphicsTeardownReadiness readiness)
	{
		if (readiness != DX12GraphicsTeardownReadiness::Unknown)
		{
			if (readiness == DX12GraphicsTeardownReadiness::DeviceRemoved)
			{
				m_state.store(
					DX12GraphicsCompletionState::Lost,
					std::memory_order_release);
			}
			m_teardown_readiness.store(readiness, std::memory_order_release);
		}
	}

	void DX12GraphicsCompletionPolicy::mark_completion_lost()
	{
		m_state.store(
			DX12GraphicsCompletionState::Lost,
			std::memory_order_release);
	}

	bool DX12GraphicsCompletionPolicy::can_issue_work() const
	{
		return m_state.load(std::memory_order_acquire) ==
			DX12GraphicsCompletionState::Trackable;
	}

	bool DX12GraphicsCompletionPolicy::can_reuse_frame_resources() const
	{
		return m_state.load(std::memory_order_acquire) ==
			DX12GraphicsCompletionState::Trackable;
	}

	bool DX12GraphicsCompletionPolicy::can_report_completion() const
	{
		return m_state.load(std::memory_order_acquire) ==
			DX12GraphicsCompletionState::Trackable;
	}

	bool DX12GraphicsCompletionPolicy::is_lost() const
	{
		return m_state.load(std::memory_order_acquire) ==
			DX12GraphicsCompletionState::Lost;
	}

	DX12GraphicsTeardownReadiness
	DX12GraphicsCompletionPolicy::cached_teardown_readiness() const
	{
		return m_teardown_readiness.load(std::memory_order_acquire);
	}

	void DX12GraphicsCompletionPolicy::reset_after_shutdown()
	{
		m_teardown_readiness.store(
			DX12GraphicsTeardownReadiness::Drained,
			std::memory_order_release);
		m_state.store(
			DX12GraphicsCompletionState::Trackable,
			std::memory_order_release);
	}

	DX12Fence::~DX12Fence()
	{
		shutdown();
	}

	bool DX12Fence::init(ID3D12Device* device, uint64_t initialValue)
	{
		m_fenceValue = initialValue;
		HRESULT hr = device->CreateFence(m_fenceValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence));
		if (FAILED(hr))
		{
			HLogError("DX12Fence: Failed to create fence. HRESULT: 0x{:08X}", (uint32_t)hr);
			return false;
		}
		dx12_set_debug_name(m_fence.Get(), "DX12 Frame Fence");

		m_fenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
		if (!m_fenceEvent)
		{
			HLogError("DX12Fence: Failed to create fence event.");
			return false;
		}

		return true;
	}

	void DX12Fence::shutdown()
	{
		if (m_fenceEvent)
		{
			CloseHandle(m_fenceEvent);
			m_fenceEvent = nullptr;
		}
		m_fence.Reset();
	}

	DX12FenceSignalResult DX12Fence::signal(ID3D12CommandQueue* queue)
	{
		DX12FenceSignalResult result{};
		if (!queue || !m_fence)
		{
			result.hresult = E_POINTER;
			return result;
		}
		if (m_fenceValue == UINT64_MAX)
		{
			result.hresult = HRESULT_FROM_WIN32(ERROR_ARITHMETIC_OVERFLOW);
			return result;
		}

		const uint64_t target_value = m_fenceValue + 1u;
		result.hresult = queue->Signal(m_fence.Get(), target_value);
		if (SUCCEEDED(result.hresult))
		{
			m_fenceValue = target_value;
			result.target_value = target_value;
		}
		return result;
	}

	DX12FenceWaitResult DX12Fence::wait(uint64_t timeout)
	{
		DX12FenceWaitResult result{};
		result.target_value = m_fenceValue;
		if (!m_fence || !m_fenceEvent)
		{
			return result;
		}

		result.completed_value = m_fence->GetCompletedValue();
		result.status = classify_dx12_fence_wait_status(
			result.completed_value,
			result.target_value,
			false,
			result.event_registration_hresult,
			false,
			result.wait_result);
		if (result.status == DX12FenceWaitStatus::Completed ||
			result.status == DX12FenceWaitStatus::DeviceRemoved)
		{
			return result;
		}

		const bool infinite_wait =
			timeout == static_cast<uint64_t>(INFINITE);
		const uint64_t maximum_finite_timeout =
			static_cast<uint64_t>(INFINITE) - 1u;
		const DWORD total_timeout = infinite_wait
			? INFINITE
			: static_cast<DWORD>(timeout > maximum_finite_timeout
				? maximum_finite_timeout
				: timeout);
		const ULONGLONG wait_start_tick = GetTickCount64();
		const ULONGLONG wait_deadline_tick = infinite_wait
			? 0u
			: wait_start_tick + static_cast<ULONGLONG>(total_timeout);

		result.event_registration_attempted = true;
		result.event_registration_hresult =
			m_fence->SetEventOnCompletion(result.target_value, m_fenceEvent);
		if (FAILED(result.event_registration_hresult))
		{
			result.completed_value = m_fence->GetCompletedValue();
			result.status = classify_dx12_fence_wait_status(
				result.completed_value,
				result.target_value,
				result.event_registration_attempted,
				result.event_registration_hresult,
				false,
				result.wait_result);
			return result;
		}

		for (;;)
		{
			DWORD remaining_timeout = INFINITE;
			if (!infinite_wait)
			{
				const ULONGLONG now_tick = GetTickCount64();
				remaining_timeout = now_tick >= wait_deadline_tick
					? 0u
					: static_cast<DWORD>(wait_deadline_tick - now_tick);
			}

			result.wait_attempted = true;
			result.wait_result =
				WaitForSingleObject(m_fenceEvent, remaining_timeout);
			if (result.wait_result == WAIT_FAILED)
			{
				result.wait_error = GetLastError();
			}
			result.completed_value = m_fence->GetCompletedValue();
			if (result.should_continue_after_wake())
			{
				// A prior finite wait can leave an older target registered on this
				// shared auto-reset event. Consuming that signal does not prove the
				// current target, so wait again without registering another event or
				// resetting the original total timeout budget.
				if (!infinite_wait && GetTickCount64() >= wait_deadline_tick)
				{
					result.wait_result = WAIT_TIMEOUT;
					result.completed_value = m_fence->GetCompletedValue();
					result.status = classify_dx12_fence_wait_status(
						result.completed_value,
						result.target_value,
						result.event_registration_attempted,
						result.event_registration_hresult,
						result.wait_attempted,
						result.wait_result);
					return result;
				}
				continue;
			}

			result.status = classify_dx12_fence_wait_status(
				result.completed_value,
				result.target_value,
				result.event_registration_attempted,
				result.event_registration_hresult,
				result.wait_attempted,
				result.wait_result);
			return result;
		}
	}

	void DX12Fence::cpu_signal(uint64_t value)
	{
		m_fenceValue = value;
		m_fence->Signal(value);
	}

	uint64_t DX12Fence::get_completed_value() const
	{
		return m_fence->GetCompletedValue();
	}
}
