#pragma once
#include "DX12Wrapper.h"

namespace RHI
{
	class DX12CommandPool
	{
	public:
		DX12CommandPool() = default;
		~DX12CommandPool() = default;

		bool init(ID3D12Device* device, D3D12_COMMAND_LIST_TYPE type);
		void shutdown();
		void reset();

		ID3D12CommandAllocator* get_allocator() const { return m_allocator.Get(); }
		D3D12_COMMAND_LIST_TYPE get_type() const { return m_type; }

	private:
		ComPtr<ID3D12CommandAllocator> m_allocator;
		D3D12_COMMAND_LIST_TYPE m_type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	};
}
