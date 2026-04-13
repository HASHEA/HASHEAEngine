#include "DX12StagingBuffer.h"
#include "Base/hlog.h"
#include "Base/hassert.h"
#include "D3D12MemAlloc.h"

namespace RHI
{
	DX12StagingBuffer::~DX12StagingBuffer()
	{
		shutdown();
	}

	bool DX12StagingBuffer::init(ID3D12Device* device, D3D12MA::Allocator* allocator, uint32_t size)
	{
		m_totalSize = size;
		m_currentOffset = 0;

		D3D12_RESOURCE_DESC resourceDesc = {};
		resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		resourceDesc.Alignment = 0;
		resourceDesc.Width = size;
		resourceDesc.Height = 1;
		resourceDesc.DepthOrArraySize = 1;
		resourceDesc.MipLevels = 1;
		resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
		resourceDesc.SampleDesc.Count = 1;
		resourceDesc.SampleDesc.Quality = 0;
		resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		resourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

		D3D12MA::ALLOCATION_DESC allocDesc = {};
		allocDesc.HeapType = D3D12_HEAP_TYPE_UPLOAD;

		HRESULT hr = allocator->CreateResource(
			&allocDesc,
			&resourceDesc,
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			&m_allocation,
			IID_PPV_ARGS(&m_resource));

		if (FAILED(hr))
		{
			HLogError("DX12StagingBuffer: Failed to create staging buffer. HRESULT: 0x{:08X}", (uint32_t)hr);
			return false;
		}

		D3D12_RANGE readRange = { 0, 0 };
		void* pData = nullptr;
		m_resource->Map(0, &readRange, &pData);
		m_mappedData = static_cast<uint8_t*>(pData);

		return true;
	}

	void DX12StagingBuffer::shutdown()
	{
		if (m_mappedData)
		{
			m_resource->Unmap(0, nullptr);
			m_mappedData = nullptr;
		}
		if (m_allocation)
		{
			m_allocation->Release();
			m_allocation = nullptr;
		}
		m_resource.Reset();
	}

	std::pair<ID3D12Resource*, uint64_t> DX12StagingBuffer::stage_data(const void* data, uint32_t size)
	{
		// Align offset to 256 bytes (D3D12 requirement for texture data placement)
		uint32_t alignedOffset = (m_currentOffset + 255) & ~255;

		if (alignedOffset + size > m_totalSize)
		{
			HLogError("DX12StagingBuffer: Out of staging memory! Requested: {}, Available: {}", size, m_totalSize - alignedOffset);
			return { nullptr, 0 };
		}

		memcpy(m_mappedData + alignedOffset, data, size);
		m_currentOffset = alignedOffset + size;

		return { m_resource.Get(), alignedOffset };
	}
}
