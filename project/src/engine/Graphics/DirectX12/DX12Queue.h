#pragma once
#include "DX12Wrapper.h"
#include "Graphics/RHICommon.h"

namespace RHI
{
	class DX12Queue
	{
	public:
		DX12Queue() = default;
		~DX12Queue() = default;

		bool init(ID3D12Device* device, D3D12_COMMAND_LIST_TYPE type);
		void shutdown();

		ID3D12CommandQueue* get_queue() const { return m_queue.Get(); }
		D3D12_COMMAND_LIST_TYPE get_type() const { return m_type; }

		void execute_command_lists(UINT count, ID3D12CommandList* const* lists);

	private:
		ComPtr<ID3D12CommandQueue> m_queue;
		D3D12_COMMAND_LIST_TYPE m_type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	};
}
