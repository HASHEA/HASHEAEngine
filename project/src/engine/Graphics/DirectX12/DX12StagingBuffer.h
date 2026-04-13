#pragma once
#include "DX12Wrapper.h"
#include <utility>

namespace D3D12MA { class Allocator; class Allocation; }

namespace RHI
{
	class DX12StagingBuffer
	{
	public:
		DX12StagingBuffer() = default;
		~DX12StagingBuffer();

		bool init(ID3D12Device* device, D3D12MA::Allocator* allocator, uint32_t size);
		void shutdown();

		// Returns (resource, offset) pair for the staged data
		std::pair<ID3D12Resource*, uint64_t> stage_data(const void* data, uint32_t size);

		// Reset offset at frame begin
		void reset() { m_currentOffset = 0; }

	private:
		ComPtr<ID3D12Resource> m_resource;
		D3D12MA::Allocation* m_allocation = nullptr;
		uint8_t* m_mappedData = nullptr;
		uint32_t m_totalSize = 0;
		uint32_t m_currentOffset = 0;
	};
}
