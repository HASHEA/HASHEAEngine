#include "DX12Texture.h"
#include "DX12TextureView.h"
#include "DX12DescriptorHeap.h"
#include "DX12Context.h"
#include "Base/hlog.h"
#include "Base/hassert.h"
#include "D3D12MemAlloc.h"

namespace RHI
{
	DX12Texture::~DX12Texture()
	{
		shutdown();
	}

	bool DX12Texture::init(const TextureCreation& ci, ID3D12Device* device, D3D12MA::Allocator* allocator, DX12DescriptorHeapManager* heapMgr)
	{
		m_creation = ci;
		m_name = ci.name ? ci.name : "";
		m_resourceState = ci.initial_state;
		m_alias = ci.alias;

		bool isDepth = DX12TextureFormat::is_depth_format(ci.format);

		D3D12_RESOURCE_DESC resourceDesc = {};
		resourceDesc.Dimension = ash_image_type_to_d3d12_dimension(ci.type);
		resourceDesc.Alignment = 0;
		resourceDesc.Width = ci.width;
		resourceDesc.Height = ci.height;
		resourceDesc.MipLevels = ci.mip_level_count;
		resourceDesc.Format = isDepth ? ash_to_dxgi_depth_resource_format(ci.format) : ash_to_dxgi_format(ci.format);
		resourceDesc.SampleDesc.Count = ash_to_d3d12_sample_count(ci.eSampleCount);
		resourceDesc.SampleDesc.Quality = 0;
		resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
		resourceDesc.Flags = ash_texture_usage_to_d3d12_flags(ci.uUsageFlags, ci.format);

		if (ci.type == Ash_Texture3D)
		{
			resourceDesc.DepthOrArraySize = ci.depth;
		}
		else if (ci.type == Ash_TextureCube || ci.type == Ash_Texture_Cube_Array)
		{
			resourceDesc.DepthOrArraySize = ci.array_layer_count;
		}
		else
		{
			resourceDesc.DepthOrArraySize = ci.array_layer_count;
		}

		D3D12MA::ALLOCATION_DESC allocDesc = {};
		allocDesc.HeapType = D3D12_HEAP_TYPE_DEFAULT;

		D3D12_RESOURCE_STATES initialState = ash_to_d3d12_resource_state(ci.initial_state);
		if (initialState == D3D12_RESOURCE_STATE_COMMON && isDepth)
			initialState = D3D12_RESOURCE_STATE_DEPTH_WRITE;

		D3D12_CLEAR_VALUE clearValue = {};
		D3D12_CLEAR_VALUE* pClearValue = nullptr;
		if ((ci.uUsageFlags & ASH_TEXTURE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0)
		{
			clearValue.Format = ash_to_dxgi_format(ci.format);
			clearValue.DepthStencil.Depth = ci.optimized_clear_depth_stencil.depth;
			clearValue.DepthStencil.Stencil = static_cast<UINT8>(ci.optimized_clear_depth_stencil.stencil);
			pClearValue = &clearValue;
		}
		else if (((ci.uUsageFlags & ASH_TEXTURE_USAGE_COLOR_ATTACHMENT_BIT) != 0) && ci.use_optimized_clear_value)
		{
			clearValue.Format = resourceDesc.Format;
			clearValue.Color[0] = ci.optimized_clear_color.float32[0];
			clearValue.Color[1] = ci.optimized_clear_color.float32[1];
			clearValue.Color[2] = ci.optimized_clear_color.float32[2];
			clearValue.Color[3] = ci.optimized_clear_color.float32[3];
			pClearValue = &clearValue;
		}

		HRESULT hr = allocator->CreateResource(
			&allocDesc,
			&resourceDesc,
			initialState,
			pClearValue,
			&m_allocation,
			IID_PPV_ARGS(&m_resource));

		if (FAILED(hr))
		{
			HLogError("DX12Texture: Failed to create texture '{}'. HRESULT: 0x{:08X}", m_name, (uint32_t)hr);
			return false;
		}

		// Match m_resourceState to the actual D3D12 initial state. For depth formats, Unknown/COMMON is
		// promoted to DEPTH_WRITE above; if we leave m_resourceState as Unknown, ash_to_d3d12 maps it to
		// COMMON and the first pass begin_barrier emits COMMON→DSVWrite while the resource is already
		// DEPTH_WRITE (RESOURCE_BARRIER_BEFORE_AFTER_MISMATCH on SceneRenderer / any depth RT pass).
		if (initialState == D3D12_RESOURCE_STATE_DEPTH_WRITE)
		{
			m_resourceState = AshResourceState::DSVWrite;
		}

		_create_default_views(device, heapMgr);
		return true;
	}

	bool DX12Texture::init_from_swapchain(ID3D12Resource* resource, AshFormat format, uint16_t width, uint16_t height, ID3D12Device* device, DX12DescriptorHeapManager* heapMgr)
	{
		m_resource = resource;
		m_isSwapchainTexture = true;
		m_resourceState = AshResourceState::Present;
		m_creation.format = format;
		m_creation.width = width;
		m_creation.height = height;
		m_creation.depth = 1;
		m_creation.array_layer_count = 1;
		m_creation.mip_level_count = 1;
		m_creation.type = Ash_Texture2D;
		m_creation.uUsageFlags = ASH_TEXTURE_USAGE_COLOR_ATTACHMENT_BIT;
		m_name = "SwapchainBackBuffer";

		_create_default_views(device, heapMgr);
		return true;
	}

	void DX12Texture::shutdown()
	{
		m_defaultRTV.reset();
		m_defaultSRV.reset();
		m_defaultUAV.reset();

		if (m_resource == nullptr && m_allocation == nullptr)
		{
			return;
		}

		ComPtr<ID3D12Resource> deferredResource = m_resource;
		D3D12MA::Allocation* deferredAllocation = m_allocation;
		const bool isSwapchainTexture = m_isSwapchainTexture;

		m_resource.Reset();
		m_allocation = nullptr;

		DX12Context* context = DX12Context::get();
		if (immediate_deletion || isSwapchainTexture || !context)
		{
			if (!isSwapchainTexture && deferredAllocation)
			{
				deferredAllocation->Release();
			}
			deferredResource.Reset();
			return;
		}

		context->get_current_frame_deletion_queue().emplace([deferredResource, deferredAllocation, isSwapchainTexture]() mutable {
			if (!isSwapchainTexture && deferredAllocation)
			{
				deferredAllocation->Release();
			}
			deferredResource.Reset();
		});
	}

	void DX12Texture::_create_default_views(ID3D12Device* device, DX12DescriptorHeapManager* heapMgr)
	{
		bool isDepth = DX12TextureFormat::is_depth_format(m_creation.format);

		// RTV for color attachments
		if (m_creation.uUsageFlags & ASH_TEXTURE_USAGE_COLOR_ATTACHMENT_BIT)
		{
			TextureViewCreation viewCI = {};
			viewCI.view_type = AshResourceViewType::ASH_RESOURCE_VIEW_TYPE_RTV;
			viewCI.view_dim = ASH_RESOURCE_VIEW_DIMENSION_TEXTURE2D;
			viewCI.format = m_creation.format;
			auto view = std::make_shared<DX12TextureView>();
			if (view->init(viewCI, std::static_pointer_cast<DX12Texture>(shared_from_this()), device, heapMgr))
				m_defaultRTV = view;
		}

		// DSV for depth attachments
		if (m_creation.uUsageFlags & ASH_TEXTURE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)
		{
			TextureViewCreation viewCI = {};
			viewCI.view_type = AshResourceViewType::ASH_RESOURCE_VIEW_TYPE_DSV;
			viewCI.view_dim = ASH_RESOURCE_VIEW_DIMENSION_TEXTURE2D;
			viewCI.format = m_creation.format;
			auto view = std::make_shared<DX12TextureView>();
			if (view->init(viewCI, std::static_pointer_cast<DX12Texture>(shared_from_this()), device, heapMgr))
				m_defaultRTV = view; // DSV goes to defaultRTV for framebuffer usage
		}

		// SRV for sampled textures
		if ((m_creation.uUsageFlags & ASH_TEXTURE_USAGE_SAMPLED_BIT) || !isDepth)
		{
			TextureViewCreation viewCI = {};
			viewCI.view_type = AshResourceViewType::ASH_RESOURCE_VIEW_TYPE_SRV;
			viewCI.format = isDepth ? m_creation.format : m_creation.format;

			// Determine view dimension from texture type
			switch (m_creation.type)
			{
			case Ash_Texture1D:           viewCI.view_dim = ASH_RESOURCE_VIEW_DIMENSION_TEXTURE1D; break;
			case Ash_Texture2D:           viewCI.view_dim = ASH_RESOURCE_VIEW_DIMENSION_TEXTURE2D; break;
			case Ash_Texture3D:           viewCI.view_dim = ASH_RESOURCE_VIEW_DIMENSION_TEXTURE3D; break;
			case Ash_TextureCube:         viewCI.view_dim = ASH_RESOURCE_VIEW_DIMENSION_TEXTURECUBE; break;
			case Ash_Texture_1D_Array:    viewCI.view_dim = ASH_RESOURCE_VIEW_DIMENSION_TEXTURE1D_ARRAY; break;
			case Ash_Texture_2D_Array:    viewCI.view_dim = ASH_RESOURCE_VIEW_DIMENSION_TEXTURE2D_ARRAY; break;
			case Ash_Texture_Cube_Array:  viewCI.view_dim = ASH_RESOURCE_VIEW_DIMENSION_TEXTURECUBE_ARRAY; break;
			default:                       viewCI.view_dim = ASH_RESOURCE_VIEW_DIMENSION_TEXTURE2D; break;
			}

			auto view = std::make_shared<DX12TextureView>();
			if (view->init(viewCI, std::static_pointer_cast<DX12Texture>(shared_from_this()), device, heapMgr))
				m_defaultSRV = view;
		}

		// UAV for storage textures
		if (m_creation.uUsageFlags & ASH_TEXTURE_USAGE_STORAGE_BIT)
		{
			TextureViewCreation viewCI = {};
			viewCI.view_type = AshResourceViewType::ASH_RESOURCE_VIEW_TYPE_UAV;
			viewCI.view_dim = ASH_RESOURCE_VIEW_DIMENSION_TEXTURE2D;
			viewCI.format = m_creation.format;
			auto view = std::make_shared<DX12TextureView>();
			if (view->init(viewCI, std::static_pointer_cast<DX12Texture>(shared_from_this()), device, heapMgr))
				m_defaultUAV = view;
		}
	}
}
