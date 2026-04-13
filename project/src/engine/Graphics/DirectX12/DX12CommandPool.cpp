#include "DX12CommandPool.h"
#include "Base/hlog.h"

namespace RHI
{
	bool DX12CommandPool::init(ID3D12Device* device, D3D12_COMMAND_LIST_TYPE type)
	{
		m_type = type;
		HRESULT hr = device->CreateCommandAllocator(type, IID_PPV_ARGS(&m_allocator));
		if (FAILED(hr))
		{
			HLogError("DX12CommandPool: Failed to create command allocator. HRESULT: 0x{:08X}", (uint32_t)hr);
			return false;
		}
		return true;
	}

	void DX12CommandPool::shutdown()
	{
		m_allocator.Reset();
	}

	void DX12CommandPool::reset()
	{
		m_allocator->Reset();
	}
}
