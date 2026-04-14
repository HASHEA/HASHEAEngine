#include "DX12Buffer.h"
#include "DX12BufferView.h"
#include "DX12DescriptorHeap.h"
#include "DX12Context.h"
#include "Base/hlog.h"
#include "Base/hassert.h"
#include "D3D12MemAlloc.h"

namespace RHI
{
	DX12Buffer::~DX12Buffer()
	{
		shutdown();
	}

	bool DX12Buffer::init(const BufferCreation& ci, ID3D12Device* device, D3D12MA::Allocator* allocator, DX12DescriptorHeapManager* heapMgr)
	{
		m_creation = ci;
		m_name = ci.name ? ci.name : "";
		m_stride = ci.struct_byte_stride;
		m_isDynamic = !ci.force_static && has_any_flags(ci.usage_flags, k_dynamic_buffer_mask);
		m_resourceState = AshResourceState::Unknown;

		D3D12_RESOURCE_DESC resourceDesc = {};
		resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		resourceDesc.Alignment = 0;
		resourceDesc.Width = ci.size;
		resourceDesc.Height = 1;
		resourceDesc.DepthOrArraySize = 1;
		resourceDesc.MipLevels = 1;
		resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
		resourceDesc.SampleDesc.Count = 1;
		resourceDesc.SampleDesc.Quality = 0;
		resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		resourceDesc.Flags = ash_buffer_usage_to_d3d12_flags(ci.usage_flags);

		D3D12MA::ALLOCATION_DESC allocDesc = {};
		D3D12_RESOURCE_STATES initialState = D3D12_RESOURCE_STATE_COMMON;

		switch (ci.access_type)
		{
		case AshResourceAccessType::ASH_RESOURCE_ACCESS_GPU_ONLY:
			allocDesc.HeapType = D3D12_HEAP_TYPE_DEFAULT;
			initialState = D3D12_RESOURCE_STATE_COMMON;
			m_resourceState = AshResourceState::Unknown;
			break;
		case AshResourceAccessType::ASH_RESOURCE_ACCESS_WRITE:
			allocDesc.HeapType = D3D12_HEAP_TYPE_UPLOAD;
			initialState = D3D12_RESOURCE_STATE_GENERIC_READ;
			m_resourceState = AshResourceState::ConstBuffer;
			break;
		case AshResourceAccessType::ASH_RESOURCE_ACCESS_READ:
			allocDesc.HeapType = D3D12_HEAP_TYPE_READBACK;
			initialState = D3D12_RESOURCE_STATE_COPY_DEST;
			m_resourceState = AshResourceState::CopyDst;
			break;
		}

		HRESULT hr = allocator->CreateResource(
			&allocDesc,
			&resourceDesc,
			initialState,
			nullptr,
			&m_allocation,
			IID_PPV_ARGS(&m_resource));

		if (FAILED(hr))
		{
			HLogError("DX12Buffer: Failed to create buffer '{}'. HRESULT: 0x{:08X}", m_name, (uint32_t)hr);
			return false;
		}

		// Persistent map for upload/readback heaps
		if (ci.access_type == AshResourceAccessType::ASH_RESOURCE_ACCESS_WRITE ||
		    ci.access_type == AshResourceAccessType::ASH_RESOURCE_ACCESS_READ)
		{
			D3D12_RANGE readRange = {};
			if (ci.access_type == AshResourceAccessType::ASH_RESOURCE_ACCESS_WRITE)
				readRange = { 0, 0 }; // CPU won't read
			void* pData = nullptr;
			m_resource->Map(0, &readRange, &pData);
			m_mappedData = static_cast<uint8_t*>(pData);
		}

		// Upload initial data
		if (ci.initial_data && ci.access_type == AshResourceAccessType::ASH_RESOURCE_ACCESS_WRITE)
		{
			memcpy(m_mappedData, ci.initial_data, ci.size);
		}

		// Create default views
		_create_default_views(device, heapMgr);

		return true;
	}

	void DX12Buffer::shutdown()
	{
		m_defaultCBV.reset();
		m_defaultSRV.reset();
		m_defaultUAV.reset();

		if (m_mappedData)
		{
			m_resource->Unmap(0, nullptr);
			m_mappedData = nullptr;
		}

		if (m_resource == nullptr && m_allocation == nullptr)
		{
			m_resourceState = AshResourceState::Unknown;
			return;
		}

		ComPtr<ID3D12Resource> deferredResource = m_resource;
		D3D12MA::Allocation* deferredAllocation = m_allocation;
		m_resource.Reset();
		m_allocation = nullptr;

		DX12Context* context = DX12Context::get();
		if (!context)
		{
			if (deferredAllocation)
			{
				deferredAllocation->Release();
			}
			deferredResource.Reset();
			m_resourceState = AshResourceState::Unknown;
			return;
		}

		context->get_current_frame_deletion_queue().emplace([deferredResource, deferredAllocation]() mutable {
			if (deferredAllocation)
			{
				deferredAllocation->Release();
			}
			deferredResource.Reset();
		});

		m_resourceState = AshResourceState::Unknown;
	}

	auto DX12Buffer::update(uint32_t offset, uint32_t size, void* pData) -> bool
	{
		if (m_mappedData)
		{
			memcpy(m_mappedData + offset, pData, size);
			return true;
		}
		if (m_creation.access_type == AshResourceAccessType::ASH_RESOURCE_ACCESS_GPU_ONLY)
		{
			DX12Context* context = DX12Context::get();
			return context ? context->queue_buffer_upload(shared_from_this(), offset, size, pData) : false;
		}
		return false;
	}

	auto DX12Buffer::get_buffer_device_address() -> uint64_t
	{
		if (m_resource)
			return m_resource->GetGPUVirtualAddress();
		return 0;
	}

	void DX12Buffer::_create_default_views(ID3D12Device* device, DX12DescriptorHeapManager* heapMgr)
	{
		// CBV for uniform buffers
		if (has_any_flags(m_creation.usage_flags, ASH_BUFFER_USAGE_UNIFORM_BUFFER_BIT))
		{
			BufferViewCreation viewCI = {};
			viewCI.view_type = AshResourceViewType::ASH_RESOURCE_VIEW_TYPE_CBV;
			viewCI.uByteRange = m_creation.size;
			auto view = std::make_shared<DX12BufferView>();
			if (view->init(viewCI, std::static_pointer_cast<DX12Buffer>(shared_from_this()), device, heapMgr))
				m_defaultCBV = view;
		}

		// SRV for storage/structured buffers
		if (has_any_flags(m_creation.usage_flags, ASH_BUFFER_USAGE_STORAGE_BUFFER_BIT | ASH_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT))
		{
			BufferViewCreation viewCI = {};
			viewCI.view_type = AshResourceViewType::ASH_RESOURCE_VIEW_TYPE_SRV;
			viewCI.uByteRange = m_creation.size;
			viewCI.uStructureStride = m_creation.struct_byte_stride;
			if (m_creation.struct_byte_stride == 0)
				viewCI.Flags = ASH_BUFFER_RESOURCE_VIEW_FLAG_RAW;
			auto view = std::make_shared<DX12BufferView>();
			if (view->init(viewCI, std::static_pointer_cast<DX12Buffer>(shared_from_this()), device, heapMgr))
				m_defaultSRV = view;
		}

		// UAV for storage buffers
		if (has_any_flags(m_creation.usage_flags, ASH_BUFFER_USAGE_STORAGE_BUFFER_BIT | ASH_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT))
		{
			BufferViewCreation viewCI = {};
			viewCI.view_type = AshResourceViewType::ASH_RESOURCE_VIEW_TYPE_UAV;
			viewCI.uByteRange = m_creation.size;
			viewCI.uStructureStride = m_creation.struct_byte_stride;
			if (m_creation.struct_byte_stride == 0)
				viewCI.Flags = ASH_BUFFER_RESOURCE_VIEW_FLAG_RAW;
			auto view = std::make_shared<DX12BufferView>();
			if (view->init(viewCI, std::static_pointer_cast<DX12Buffer>(shared_from_this()), device, heapMgr))
				m_defaultUAV = view;
		}
	}
}
