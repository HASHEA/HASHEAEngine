#include "DX12TextureView.h"
#include "DX12Texture.h"
#include "DX12DescriptorHeap.h"
#include "DX12Context.h"
#include "Base/hlog.h"

namespace RHI
{
	DX12TextureView::~DX12TextureView()
	{
		shutdown();
	}

	bool DX12TextureView::init(const TextureViewCreation& ci, std::shared_ptr<DX12Texture> parentTexture, ID3D12Device* device, DX12DescriptorHeapManager* heapMgr)
	{
		m_parentTexture = parentTexture;
		m_viewDesc = ci;
		m_heapMgr = heapMgr;

		bool isDepth = DX12TextureFormat::is_depth_format(ci.format);
		uint32_t mipLevels = ci.sub_resource.uMipCount == AshSubresourceRange::s_All ?
			parentTexture->get_mip_maps_count() - ci.sub_resource.uBaseMipLevel : ci.sub_resource.uMipCount;
		uint32_t arrayLayers = ci.sub_resource.uArrayCount == AshSubresourceRange::s_All ?
			parentTexture->get_layer_count() - ci.sub_resource.uBaseArraySlice : ci.sub_resource.uArrayCount;

		switch (ci.view_type)
		{
		case AshResourceViewType::ASH_RESOURCE_VIEW_TYPE_SRV:
		{
			m_descriptorHandle = heapMgr->cpuCbvSrvUav.allocate();
			D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
			srvDesc.Format = isDepth ? ash_to_dxgi_depth_srv_format(ci.format) : ash_to_dxgi_format(ci.format);
			srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
			srvDesc.ViewDimension = ash_view_dim_to_d3d12_srv_dim(ci.view_dim);

			switch (ci.view_dim)
			{
			case ASH_RESOURCE_VIEW_DIMENSION_TEXTURE1D:
				srvDesc.Texture1D.MostDetailedMip = ci.sub_resource.uBaseMipLevel;
				srvDesc.Texture1D.MipLevels = mipLevels;
				break;
			case ASH_RESOURCE_VIEW_DIMENSION_TEXTURE2D:
				srvDesc.Texture2D.MostDetailedMip = ci.sub_resource.uBaseMipLevel;
				srvDesc.Texture2D.MipLevels = mipLevels;
				srvDesc.Texture2D.PlaneSlice = 0;
				break;
			case ASH_RESOURCE_VIEW_DIMENSION_TEXTURE3D:
				srvDesc.Texture3D.MostDetailedMip = ci.sub_resource.uBaseMipLevel;
				srvDesc.Texture3D.MipLevels = mipLevels;
				break;
			case ASH_RESOURCE_VIEW_DIMENSION_TEXTURECUBE:
				srvDesc.TextureCube.MostDetailedMip = ci.sub_resource.uBaseMipLevel;
				srvDesc.TextureCube.MipLevels = mipLevels;
				break;
			case ASH_RESOURCE_VIEW_DIMENSION_TEXTURE1D_ARRAY:
				srvDesc.Texture1DArray.MostDetailedMip = ci.sub_resource.uBaseMipLevel;
				srvDesc.Texture1DArray.MipLevels = mipLevels;
				srvDesc.Texture1DArray.FirstArraySlice = ci.sub_resource.uBaseArraySlice;
				srvDesc.Texture1DArray.ArraySize = arrayLayers;
				break;
			case ASH_RESOURCE_VIEW_DIMENSION_TEXTURE2D_ARRAY:
				srvDesc.Texture2DArray.MostDetailedMip = ci.sub_resource.uBaseMipLevel;
				srvDesc.Texture2DArray.MipLevels = mipLevels;
				srvDesc.Texture2DArray.FirstArraySlice = ci.sub_resource.uBaseArraySlice;
				srvDesc.Texture2DArray.ArraySize = arrayLayers;
				srvDesc.Texture2DArray.PlaneSlice = 0;
				break;
			case ASH_RESOURCE_VIEW_DIMENSION_TEXTURECUBE_ARRAY:
				srvDesc.TextureCubeArray.MostDetailedMip = ci.sub_resource.uBaseMipLevel;
				srvDesc.TextureCubeArray.MipLevels = mipLevels;
				srvDesc.TextureCubeArray.First2DArrayFace = ci.sub_resource.uBaseArraySlice;
				srvDesc.TextureCubeArray.NumCubes = arrayLayers / 6;
				break;
			default: break;
			}

			device->CreateShaderResourceView(parentTexture->get_resource(), &srvDesc, m_descriptorHandle.cpuHandle);
			break;
		}
		case AshResourceViewType::ASH_RESOURCE_VIEW_TYPE_RTV:
		{
			m_descriptorHandle = heapMgr->cpuRtv.allocate();
			D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
			rtvDesc.Format = ash_to_dxgi_format(ci.format);
			rtvDesc.ViewDimension = ash_view_dim_to_d3d12_rtv_dim(ci.view_dim);

			switch (ci.view_dim)
			{
			case ASH_RESOURCE_VIEW_DIMENSION_TEXTURE1D:
				rtvDesc.Texture1D.MipSlice = ci.sub_resource.uBaseMipLevel;
				break;
			case ASH_RESOURCE_VIEW_DIMENSION_TEXTURE2D:
				rtvDesc.Texture2D.MipSlice = ci.sub_resource.uBaseMipLevel;
				rtvDesc.Texture2D.PlaneSlice = 0;
				break;
			case ASH_RESOURCE_VIEW_DIMENSION_TEXTURE3D:
				rtvDesc.Texture3D.MipSlice = ci.sub_resource.uBaseMipLevel;
				rtvDesc.Texture3D.FirstWSlice = ci.sub_resource.uBaseArraySlice;
				rtvDesc.Texture3D.WSize = arrayLayers;
				break;
			case ASH_RESOURCE_VIEW_DIMENSION_TEXTURE1D_ARRAY:
				rtvDesc.Texture1DArray.MipSlice = ci.sub_resource.uBaseMipLevel;
				rtvDesc.Texture1DArray.FirstArraySlice = ci.sub_resource.uBaseArraySlice;
				rtvDesc.Texture1DArray.ArraySize = arrayLayers;
				break;
			case ASH_RESOURCE_VIEW_DIMENSION_TEXTURE2D_ARRAY:
				rtvDesc.Texture2DArray.MipSlice = ci.sub_resource.uBaseMipLevel;
				rtvDesc.Texture2DArray.FirstArraySlice = ci.sub_resource.uBaseArraySlice;
				rtvDesc.Texture2DArray.ArraySize = arrayLayers;
				rtvDesc.Texture2DArray.PlaneSlice = 0;
				break;
			default: break;
			}

			device->CreateRenderTargetView(parentTexture->get_resource(), &rtvDesc, m_descriptorHandle.cpuHandle);
			break;
		}
		case AshResourceViewType::ASH_RESOURCE_VIEW_TYPE_DSV:
		{
			m_descriptorHandle = heapMgr->cpuDsv.allocate();
			D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
			dsvDesc.Format = ash_to_dxgi_format(ci.format);
			dsvDesc.ViewDimension = ash_view_dim_to_d3d12_dsv_dim(ci.view_dim);
			dsvDesc.Flags = ci.read_only_depth ? D3D12_DSV_FLAG_READ_ONLY_DEPTH : D3D12_DSV_FLAG_NONE;

			switch (ci.view_dim)
			{
			case ASH_RESOURCE_VIEW_DIMENSION_TEXTURE1D:
				dsvDesc.Texture1D.MipSlice = ci.sub_resource.uBaseMipLevel;
				break;
			case ASH_RESOURCE_VIEW_DIMENSION_TEXTURE2D:
				dsvDesc.Texture2D.MipSlice = ci.sub_resource.uBaseMipLevel;
				break;
			case ASH_RESOURCE_VIEW_DIMENSION_TEXTURE1D_ARRAY:
				dsvDesc.Texture1DArray.MipSlice = ci.sub_resource.uBaseMipLevel;
				dsvDesc.Texture1DArray.FirstArraySlice = ci.sub_resource.uBaseArraySlice;
				dsvDesc.Texture1DArray.ArraySize = arrayLayers;
				break;
			case ASH_RESOURCE_VIEW_DIMENSION_TEXTURE2D_ARRAY:
				dsvDesc.Texture2DArray.MipSlice = ci.sub_resource.uBaseMipLevel;
				dsvDesc.Texture2DArray.FirstArraySlice = ci.sub_resource.uBaseArraySlice;
				dsvDesc.Texture2DArray.ArraySize = arrayLayers;
				break;
			default: break;
			}

			device->CreateDepthStencilView(parentTexture->get_resource(), &dsvDesc, m_descriptorHandle.cpuHandle);
			break;
		}
		case AshResourceViewType::ASH_RESOURCE_VIEW_TYPE_UAV:
		{
			m_descriptorHandle = heapMgr->cpuCbvSrvUav.allocate();
			D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
			uavDesc.Format = ash_to_dxgi_format(ci.format);
			uavDesc.ViewDimension = ash_view_dim_to_d3d12_uav_dim(ci.view_dim);

			switch (ci.view_dim)
			{
			case ASH_RESOURCE_VIEW_DIMENSION_TEXTURE1D:
				uavDesc.Texture1D.MipSlice = ci.sub_resource.uBaseMipLevel;
				break;
			case ASH_RESOURCE_VIEW_DIMENSION_TEXTURE2D:
				uavDesc.Texture2D.MipSlice = ci.sub_resource.uBaseMipLevel;
				uavDesc.Texture2D.PlaneSlice = 0;
				break;
			case ASH_RESOURCE_VIEW_DIMENSION_TEXTURE3D:
				uavDesc.Texture3D.MipSlice = ci.sub_resource.uBaseMipLevel;
				uavDesc.Texture3D.FirstWSlice = ci.sub_resource.uBaseArraySlice;
				uavDesc.Texture3D.WSize = arrayLayers;
				break;
			case ASH_RESOURCE_VIEW_DIMENSION_TEXTURE1D_ARRAY:
				uavDesc.Texture1DArray.MipSlice = ci.sub_resource.uBaseMipLevel;
				uavDesc.Texture1DArray.FirstArraySlice = ci.sub_resource.uBaseArraySlice;
				uavDesc.Texture1DArray.ArraySize = arrayLayers;
				break;
			case ASH_RESOURCE_VIEW_DIMENSION_TEXTURE2D_ARRAY:
				uavDesc.Texture2DArray.MipSlice = ci.sub_resource.uBaseMipLevel;
				uavDesc.Texture2DArray.FirstArraySlice = ci.sub_resource.uBaseArraySlice;
				uavDesc.Texture2DArray.ArraySize = arrayLayers;
				uavDesc.Texture2DArray.PlaneSlice = 0;
				break;
			default: break;
			}

			device->CreateUnorderedAccessView(parentTexture->get_resource(), nullptr, &uavDesc, m_descriptorHandle.cpuHandle);
			break;
		}
		default:
			HLogError("DX12TextureView: Unsupported view type.");
			return false;
		}

		return true;
	}

	void DX12TextureView::shutdown()
	{
		if (m_heapMgr && m_descriptorHandle.is_valid())
		{
			const auto descriptorHandle = m_descriptorHandle;
			const auto viewType = m_viewDesc.view_type;
			DX12DescriptorHeapManager* heapMgr = m_heapMgr;

			auto free_descriptor = [heapMgr, descriptorHandle, viewType]() {
				if (!heapMgr || !descriptorHandle.is_valid())
				{
					return;
				}

				if (viewType == AshResourceViewType::ASH_RESOURCE_VIEW_TYPE_RTV)
				{
					heapMgr->cpuRtv.free(descriptorHandle);
				}
				else if (viewType == AshResourceViewType::ASH_RESOURCE_VIEW_TYPE_DSV)
				{
					heapMgr->cpuDsv.free(descriptorHandle);
				}
				else
				{
					heapMgr->cpuCbvSrvUav.free(descriptorHandle);
				}
			};

			DX12Context* context = DX12Context::get();
			if (immediate_deletion || !context)
			{
				free_descriptor();
			}
			else
			{
				context->get_current_frame_deletion_queue().emplace(std::move(free_descriptor));
			}

			m_descriptorHandle = {};
		}
		m_parentTexture.reset();
	}
}
