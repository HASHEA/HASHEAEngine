#include "DX12Queue.h"
#include "Base/hlog.h"

namespace RHI
{
	bool DX12Queue::init(ID3D12Device* device, D3D12_COMMAND_LIST_TYPE type)
	{
		m_type = type;
		D3D12_COMMAND_QUEUE_DESC queueDesc = {};
		queueDesc.Type = type;
		queueDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
		queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
		queueDesc.NodeMask = 0;

		HRESULT hr = device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_queue));
		if (FAILED(hr))
		{
			HLogError("DX12Queue: Failed to create command queue. HRESULT: 0x{:08X}", (uint32_t)hr);
			return false;
		}
		return true;
	}

	void DX12Queue::shutdown()
	{
		m_queue.Reset();
	}

	void DX12Queue::execute_command_lists(UINT count, ID3D12CommandList* const* lists)
	{
		m_queue->ExecuteCommandLists(count, lists);
	}
}
