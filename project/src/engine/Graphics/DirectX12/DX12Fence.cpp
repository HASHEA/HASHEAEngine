#include "DX12Fence.h"
#include "Base/hlog.h"
#include "Base/hassert.h"

namespace RHI
{
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

	void DX12Fence::signal(ID3D12CommandQueue* queue)
	{
		uint64_t signaled_value = m_fenceValue;
		const HRESULT signal_result = signal_checked(queue, signaled_value);
		if (FAILED(signal_result))
		{
			HLogError(
				"DX12Fence: Failed to signal command queue. HRESULT: 0x{:08X}",
				static_cast<uint32_t>(signal_result));
		}
	}

	auto DX12Fence::signal_checked(ID3D12CommandQueue* queue, uint64_t& out_value) -> HRESULT
	{
		out_value = m_fenceValue;
		if (!queue || !m_fence)
		{
			return E_POINTER;
		}
		if (m_fenceValue == UINT64_MAX)
		{
			return E_FAIL;
		}

		const uint64_t candidate_value = m_fenceValue + 1u;
		const HRESULT signal_result = queue->Signal(m_fence.Get(), candidate_value);
		if (SUCCEEDED(signal_result))
		{
			m_fenceValue = candidate_value;
			out_value = candidate_value;
		}
		return signal_result;
	}

	void DX12Fence::wait(uint64_t timeout)
	{
		if (m_fence->GetCompletedValue() < m_fenceValue)
		{
			m_fence->SetEventOnCompletion(m_fenceValue, m_fenceEvent);
			WaitForSingleObject(m_fenceEvent, static_cast<DWORD>(timeout));
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
