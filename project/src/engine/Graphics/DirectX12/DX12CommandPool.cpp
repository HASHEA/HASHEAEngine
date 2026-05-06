#include "DX12CommandPool.h"
#include "Base/hlog.h"

namespace RHI
{
	namespace
	{
		const char* command_allocator_type_name(D3D12_COMMAND_LIST_TYPE type)
		{
			switch (type)
			{
			case D3D12_COMMAND_LIST_TYPE_DIRECT:
				return "Direct";
			case D3D12_COMMAND_LIST_TYPE_COMPUTE:
				return "Compute";
			case D3D12_COMMAND_LIST_TYPE_COPY:
				return "Copy";
			default:
				return "Unknown";
			}
		}
	}

	bool DX12CommandPool::init(ID3D12Device* device, D3D12_COMMAND_LIST_TYPE type)
	{
		m_type = type;
		HRESULT hr = device->CreateCommandAllocator(type, IID_PPV_ARGS(&m_allocator));
		if (FAILED(hr))
		{
			HLogError("DX12CommandPool: Failed to create command allocator. HRESULT: 0x{:08X}", (uint32_t)hr);
			return false;
		}
		const std::string debugName = std::string("DX12 ") + command_allocator_type_name(type) + " Command Allocator";
		dx12_set_debug_name(m_allocator.Get(), debugName.c_str());
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
