#include "DX12BufferView.h"
#include "DX12Buffer.h"
#include "DX12DescriptorHeap.h"
#include "Base/hlog.h"

namespace RHI
{
	DX12BufferView::~DX12BufferView()
	{
		shutdown();
	}

	bool DX12BufferView::init(const BufferViewCreation& ci, std::shared_ptr<DX12Buffer> parentBuffer, ID3D12Device* device, DX12DescriptorHeapManager* heapMgr)
	{
		m_parentBuffer = parentBuffer;
		m_viewDesc = ci;
		m_heapMgr = heapMgr;

		uint32_t bufferSize = ci.uByteRange > 0 ? ci.uByteRange : parentBuffer->get_size();
		uint32_t byteOffset = ci.uByteOffset;

		switch (ci.view_type)
		{
		case AshResourceViewType::ASH_RESOURCE_VIEW_TYPE_CBV:
		{
			m_descriptorHandle = heapMgr->cpuCbvSrvUav.allocate();
			D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
			cbvDesc.BufferLocation = parentBuffer->get_resource()->GetGPUVirtualAddress() + byteOffset;
			cbvDesc.SizeInBytes = (bufferSize + 255) & ~255; // Must be 256-byte aligned
			device->CreateConstantBufferView(&cbvDesc, m_descriptorHandle.cpuHandle);
			break;
		}
		case AshResourceViewType::ASH_RESOURCE_VIEW_TYPE_SRV:
		{
			m_descriptorHandle = heapMgr->cpuCbvSrvUav.allocate();
			D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
			srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
			srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

			if (ci.Flags & ASH_BUFFER_RESOURCE_VIEW_FLAG_RAW)
			{
				srvDesc.Format = DXGI_FORMAT_R32_TYPELESS;
				srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
				srvDesc.Buffer.FirstElement = byteOffset / 4;
				srvDesc.Buffer.NumElements = bufferSize / 4;
			}
			else if (ci.uStructureStride > 0)
			{
				srvDesc.Format = DXGI_FORMAT_UNKNOWN;
				srvDesc.Buffer.StructureByteStride = ci.uStructureStride;
				srvDesc.Buffer.FirstElement = byteOffset / ci.uStructureStride;
				srvDesc.Buffer.NumElements = bufferSize / ci.uStructureStride;
			}
			else
			{
				srvDesc.Format = ash_to_dxgi_format(ci.format);
				uint32_t elementSize = get_dxgi_format_info(ci.format).uBytesPerBlock;
				if (elementSize == 0) elementSize = 4;
				srvDesc.Buffer.FirstElement = byteOffset / elementSize;
				srvDesc.Buffer.NumElements = bufferSize / elementSize;
			}

			device->CreateShaderResourceView(parentBuffer->get_resource(), &srvDesc, m_descriptorHandle.cpuHandle);
			break;
		}
		case AshResourceViewType::ASH_RESOURCE_VIEW_TYPE_UAV:
		{
			m_descriptorHandle = heapMgr->cpuCbvSrvUav.allocate();
			D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
			uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;

			if (ci.Flags & ASH_BUFFER_RESOURCE_VIEW_FLAG_RAW)
			{
				uavDesc.Format = DXGI_FORMAT_R32_TYPELESS;
				uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
				uavDesc.Buffer.FirstElement = byteOffset / 4;
				uavDesc.Buffer.NumElements = bufferSize / 4;
			}
			else if (ci.uStructureStride > 0)
			{
				uavDesc.Format = DXGI_FORMAT_UNKNOWN;
				uavDesc.Buffer.StructureByteStride = ci.uStructureStride;
				uavDesc.Buffer.FirstElement = byteOffset / ci.uStructureStride;
				uavDesc.Buffer.NumElements = bufferSize / ci.uStructureStride;
			}
			else
			{
				uavDesc.Format = ash_to_dxgi_format(ci.format);
				uint32_t elementSize = get_dxgi_format_info(ci.format).uBytesPerBlock;
				if (elementSize == 0) elementSize = 4;
				uavDesc.Buffer.FirstElement = byteOffset / elementSize;
				uavDesc.Buffer.NumElements = bufferSize / elementSize;
			}

			device->CreateUnorderedAccessView(parentBuffer->get_resource(), nullptr, &uavDesc, m_descriptorHandle.cpuHandle);
			break;
		}
		default:
			HLogError("DX12BufferView: Unsupported view type.");
			return false;
		}

		return true;
	}

	void DX12BufferView::shutdown()
	{
		if (m_heapMgr && m_descriptorHandle.is_valid())
		{
			m_heapMgr->cpuCbvSrvUav.free(m_descriptorHandle);
			m_descriptorHandle = {};
		}
		m_parentBuffer.reset();
	}
}
