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
		m_fenceValue++;
		queue->Signal(m_fence.Get(), m_fenceValue);
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
