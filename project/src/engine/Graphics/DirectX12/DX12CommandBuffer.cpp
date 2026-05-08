#include "DX12CommandBuffer.h"
#include "DX12Context.h"
#include "DX12Buffer.h"
#include "DX12Texture.h"
#include "DX12Framebuffer.h"
#include "DX12RenderPass.h"
#include "DX12ResourceTracker.h"
#include "DX12StagingBufferPool.h"
#include "Base/hlog.h"
#include "Base/hassert.h"
#include "Base/hprofiler.h"
#include "Graphics/Framebuffer.h"
#include "Graphics/TextureUploadUtils.h"
#include <cstring>

namespace RHI
{
	namespace
	{
		const char* command_list_type_name(D3D12_COMMAND_LIST_TYPE type)
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

	DX12CommandBuffer::~DX12CommandBuffer()
	{
		shutdown();
	}

	bool DX12CommandBuffer::init(ID3D12Device4* device, D3D12_COMMAND_LIST_TYPE type)
	{
		// Create command list in closed state initially
		HRESULT hr = device->CreateCommandList1(0, type, D3D12_COMMAND_LIST_FLAG_NONE, IID_PPV_ARGS(&m_cmdList));
		if (FAILED(hr))
		{
			HLogError("DX12CommandBuffer: Failed to create command list. HRESULT: 0x{:08X}", (uint32_t)hr);
			return false;
		}
		const std::string debugName = std::string("DX12 ") + command_list_type_name(type) + " Command List";
		dx12_set_debug_name(m_cmdList.Get(), debugName.c_str());
		return true;
	}

	void DX12CommandBuffer::shutdown()
	{
		m_cmdList.Reset();
	}

	auto DX12CommandBuffer::begin_record() -> void
	{
		H_ASSERT(m_currentAllocator != nullptr);
		m_cmdList->Reset(m_currentAllocator, nullptr);
		m_state = ASH_Recording;

		// Set descriptor heaps
		auto& heaps = DX12Context::get()->get_descriptor_heaps();
		ID3D12DescriptorHeap* descriptorHeaps[] = {
			heaps.gpuCbvSrvUav.get_heap(),
			heaps.gpuSampler.get_heap()
		};
		m_cmdList->SetDescriptorHeaps(2, descriptorHeaps);
	}

	auto DX12CommandBuffer::end_record() -> void
	{
		m_cmdList->Close();
		m_state = ASH_Ended;
	}

	void DX12CommandBuffer::_apply_barriers(const AshBarrier* barriers, uint32_t count)
	{
		ASH_PROFILE_SCOPE_NC("DX12CommandBuffer::ApplyBarriers", AshEngine::Profile::Color::Barrier);
		ASH_PROFILE_SCOPE_VALUE(static_cast<uint64_t>(count));
		std::vector<D3D12_RESOURCE_BARRIER> d3dBarriers;
		d3dBarriers.reserve(count);

		for (uint32_t i = 0; i < count; ++i)
		{
			const auto& barrier = barriers[i];
			D3D12_RESOURCE_BARRIER d3dBarrier = {};

			if (barrier.eType == AshBarrier::EType::Texture && barrier.pTexture)
			{
				auto* dx12Tex = static_cast<DX12Texture*>(barrier.pTexture.get());
				ID3D12Resource* resource = dx12Tex->get_resource();
				if (!resource) continue;

				D3D12_RESOURCE_STATES stateBefore = ash_to_d3d12_resource_state(barrier.eSRCAccess);
				D3D12_RESOURCE_STATES stateAfter = ash_to_d3d12_resource_state(barrier.eDSTAccess);

				const uint32_t mipLevels = dx12Tex->get_mip_maps_count();
				const uint32_t arraySize = dx12Tex->get_type() == Ash_Texture3D ? 1u : dx12Tex->get_layer_count();
				const AshSubresourceRange resolvedRange = dx12Tex->resolve_subresource_range(barrier);
				const bool wholeResource =
					resolvedRange.uBaseMipLevel == 0 &&
					resolvedRange.uBaseArraySlice == 0 &&
					resolvedRange.uMipCount == mipLevels &&
					resolvedRange.uArrayCount == arraySize;

				DX12Context* context = DX12Context::get();
				if (!context)
				{
					continue;
				}
				DX12ResourceTracker& resourceTracker = context->get_resource_tracker();
				if (!resourceTracker.is_tracked(resource))
				{
					resourceTracker.track_resource(
						resource,
						ash_to_d3d12_resource_state(dx12Tex->get_resource_state()),
						dx12Tex->get_subresource_count());
				}

				d3dBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
				d3dBarrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
				d3dBarrier.Transition.pResource = resource;
				d3dBarrier.Transition.StateAfter = stateAfter;

				if (barrier.eSRCAccess == AshResourceState::Unknown)
				{
					if (wholeResource)
					{
						resourceTracker.generate_barriers(resource, stateAfter, d3dBarriers);
					}
					else
					{
						const uint32_t mipEnd = resolvedRange.uBaseMipLevel + resolvedRange.uMipCount;
						const uint32_t sliceEnd = resolvedRange.uBaseArraySlice + resolvedRange.uArrayCount;
						for (uint32_t slice = resolvedRange.uBaseArraySlice; slice < sliceEnd; ++slice)
						{
							for (uint32_t mip = resolvedRange.uBaseMipLevel; mip < mipEnd; ++mip)
							{
								const uint32_t subresource = mip + slice * mipLevels;
								resourceTracker.generate_barriers(resource, stateAfter, d3dBarriers, subresource);
							}
						}
					}
				}
				else
				{
					d3dBarrier.Transition.StateBefore = stateBefore;
					if (wholeResource)
					{
						if (stateBefore != stateAfter)
						{
							d3dBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
							d3dBarriers.push_back(d3dBarrier);
						}
						resourceTracker.set_state(resource, stateAfter);
					}
					else
					{
						const uint32_t mipEnd = resolvedRange.uBaseMipLevel + resolvedRange.uMipCount;
						const uint32_t sliceEnd = resolvedRange.uBaseArraySlice + resolvedRange.uArrayCount;
						for (uint32_t slice = resolvedRange.uBaseArraySlice; slice < sliceEnd; ++slice)
						{
							for (uint32_t mip = resolvedRange.uBaseMipLevel; mip < mipEnd; ++mip)
							{
								const uint32_t subresource = mip + slice * mipLevels;
								if (stateBefore != stateAfter)
								{
									d3dBarrier.Transition.Subresource = subresource;
									d3dBarriers.push_back(d3dBarrier);
								}
								resourceTracker.set_state(resource, stateAfter, subresource);
							}
						}
					}
				}

				dx12Tex->set_resource_state(wholeResource ? barrier.eDSTAccess : AshResourceState::Unknown);
			}
			else if (barrier.eType == AshBarrier::EType::Buffer && barrier.pBuffer)
			{
				auto* dx12Buf = static_cast<DX12Buffer*>(barrier.pBuffer.get());
				ID3D12Resource* resource = dx12Buf->get_resource();
				if (!resource) continue;

				const BufferCreation& bufferCreation = dx12Buf->get_buffer_creation_info();
				if (bufferCreation.access_type != AshResourceAccessType::ASH_RESOURCE_ACCESS_GPU_ONLY)
				{
					dx12Buf->set_resource_state(barrier.eDSTAccess);
					continue;
				}

				D3D12_RESOURCE_STATES stateBefore = ash_to_d3d12_resource_state(barrier.eSRCAccess);
				D3D12_RESOURCE_STATES stateAfter = ash_to_d3d12_resource_state(barrier.eDSTAccess);

				if (barrier.eSRCAccess == AshResourceState::Unknown)
				{
					stateBefore = ash_to_d3d12_resource_state(dx12Buf->get_resource_state());
				}

				if (stateBefore == stateAfter) continue;

				d3dBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
				d3dBarrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
				d3dBarrier.Transition.pResource = resource;
				d3dBarrier.Transition.StateBefore = stateBefore;
				d3dBarrier.Transition.StateAfter = stateAfter;
				d3dBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

				dx12Buf->set_resource_state(barrier.eDSTAccess);
				d3dBarriers.push_back(d3dBarrier);
			}
		}

		if (!d3dBarriers.empty())
		{
			m_cmdList->ResourceBarrier(static_cast<UINT>(d3dBarriers.size()), d3dBarriers.data());
		}
	}

	auto DX12CommandBuffer::cmd_transition_resource_state(const AshBarrier& barrierInfo) -> bool
	{
		_apply_barriers(&barrierInfo, 1);
		return true;
	}

	auto DX12CommandBuffer::cmd_transition_resource_state(const std::initializer_list<AshBarrier>& lsBarrierInfoArray) -> bool
	{
		_apply_barriers(lsBarrierInfoArray.begin(), static_cast<uint32_t>(lsBarrierInfoArray.size()));
		return true;
	}

	auto DX12CommandBuffer::cmd_transition_resource_state(const AshBarrier* pBarrierInfo, uint32_t uBarrierCount) -> bool
	{
		_apply_barriers(pBarrierInfo, uBarrierCount);
		return true;
	}

	auto DX12CommandBuffer::cmd_begin_render_pass(std::shared_ptr<Framebuffer> frameBuffer) -> void
	{
		m_currentFramebuffer = frameBuffer;
		auto* dx12Fb = static_cast<DX12Framebuffer*>(frameBuffer.get());

		uint32_t numRTs = dx12Fb->get_rtv_count();
		const D3D12_CPU_DESCRIPTOR_HANDLE* rtvHandles = dx12Fb->get_rtv_handles();
		const D3D12_CPU_DESCRIPTOR_HANDLE* dsvHandle = dx12Fb->has_dsv() ? dx12Fb->get_dsv_handle() : nullptr;

		m_cmdList->OMSetRenderTargets(numRTs, rtvHandles, FALSE, dsvHandle);

		auto renderPass = frameBuffer->get_render_pass();
		auto* dx12RP = static_cast<DX12RenderPass*>(renderPass.get());

		// Clear render targets
		for (uint32_t i = 0; i < numRTs; ++i)
		{
			auto loadOp = dx12RP->get_color_load_op(i);
			if (loadOp == ASH_LOAD_CLEAR)
			{
				const auto& clearColor = frameBuffer->get_render_target_clear_color(i);
				m_cmdList->ClearRenderTargetView(rtvHandles[i], clearColor.float32, 0, nullptr);
			}
		}

		// Clear depth stencil
		if (dsvHandle)
		{
			auto depthLoadOp = dx12RP->get_depth_stencil_operations();
			if (depthLoadOp == ASH_LOAD_CLEAR)
			{
				const auto& dsv = frameBuffer->get_depth_stencil_clear_color();
				D3D12_CLEAR_FLAGS clearFlags = D3D12_CLEAR_FLAG_DEPTH;
				if (DX12TextureFormat::has_stencil(dx12RP->get_depth_stencil_format()))
					clearFlags |= D3D12_CLEAR_FLAG_STENCIL;
				m_cmdList->ClearDepthStencilView(*dsvHandle, clearFlags, dsv.depth, static_cast<UINT8>(dsv.stencil), 0, nullptr);
			}
		}
	}

	auto DX12CommandBuffer::cmd_end_render_pass() -> void
	{
		m_currentFramebuffer = nullptr;
	}

	auto DX12CommandBuffer::cmd_bind_pipeline() -> void
	{
		// Pipeline binding is handled by RenderProgram::apply()
	}

	auto DX12CommandBuffer::cmd_set_viewport(const Viewport& viewport) -> void
	{
		D3D12_VIEWPORT vp = {};
		vp.TopLeftX = static_cast<float>(viewport.rect.x);
		vp.TopLeftY = static_cast<float>(viewport.rect.y);
		vp.Width = static_cast<float>(viewport.rect.width);
		vp.Height = static_cast<float>(viewport.rect.height);
		vp.MinDepth = viewport.min_depth;
		vp.MaxDepth = viewport.max_depth;
		m_cmdList->RSSetViewports(1, &vp);
	}

	auto DX12CommandBuffer::cmd_set_scissor(const Rect2DInt& scissor) -> void
	{
		D3D12_RECT rect = {};
		rect.left = scissor.x;
		rect.top = scissor.y;
		rect.right = scissor.x + scissor.width;
		rect.bottom = scissor.y + scissor.height;
		m_cmdList->RSSetScissorRects(1, &rect);
	}

	auto DX12CommandBuffer::cmd_bind_vertex_buffers(uint32_t firstBinding, uint32_t bindingCount, std::shared_ptr<Buffer>* buffers, const uint64_t* offsets) -> void
	{
		std::vector<D3D12_VERTEX_BUFFER_VIEW> vbViews(bindingCount);
		for (uint32_t i = 0; i < bindingCount; ++i)
		{
			auto* dx12Buf = static_cast<DX12Buffer*>(buffers[i].get());
			vbViews[i].BufferLocation = dx12Buf->get_resource()->GetGPUVirtualAddress() + (offsets ? offsets[i] : 0);
			vbViews[i].SizeInBytes = dx12Buf->get_size() - static_cast<UINT>(offsets ? offsets[i] : 0);
			vbViews[i].StrideInBytes = dx12Buf->get_stride();
		}
		m_cmdList->IASetVertexBuffers(firstBinding, bindingCount, vbViews.data());
	}

	auto DX12CommandBuffer::cmd_bind_index_buffer(std::shared_ptr<Buffer> buffer, uint64_t offset, AshIndexType indexType) -> void
	{
		auto* dx12Buf = static_cast<DX12Buffer*>(buffer.get());
		D3D12_INDEX_BUFFER_VIEW ibView = {};
		ibView.BufferLocation = dx12Buf->get_resource()->GetGPUVirtualAddress() + offset;
		ibView.SizeInBytes = dx12Buf->get_size() - static_cast<UINT>(offset);
		ibView.Format = ash_to_d3d12_index_type(indexType);
		m_cmdList->IASetIndexBuffer(&ibView);
	}

	auto DX12CommandBuffer::cmd_draw(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance) -> void
	{
		m_cmdList->DrawInstanced(vertexCount, instanceCount, firstVertex, firstInstance);
	}

	auto DX12CommandBuffer::cmd_draw_indexed(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance) -> void
	{
		m_cmdList->DrawIndexedInstanced(indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
	}

	auto DX12CommandBuffer::cmd_dispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) -> void
	{
		m_cmdList->Dispatch(groupCountX, groupCountY, groupCountZ);
	}

	auto DX12CommandBuffer::cmd_copy_texture(std::shared_ptr<Texture> source, std::shared_ptr<Texture> destination) -> bool
	{
		if (!source || !destination)
		{
			return false;
		}

		auto* sourceTexture = static_cast<DX12Texture*>(source.get());
		auto* destinationTexture = static_cast<DX12Texture*>(destination.get());
		ID3D12Resource* sourceResource = sourceTexture ? sourceTexture->get_resource() : nullptr;
		ID3D12Resource* destinationResource = destinationTexture ? destinationTexture->get_resource() : nullptr;
		if (!sourceResource || !destinationResource)
		{
			return false;
		}

		auto canonical_format = [](DXGI_FORMAT format) -> DXGI_FORMAT
			{
				switch (format)
				{
				case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
					return DXGI_FORMAT_R8G8B8A8_UNORM;
				case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
					return DXGI_FORMAT_B8G8R8A8_UNORM;
				case DXGI_FORMAT_BC1_UNORM_SRGB:
					return DXGI_FORMAT_BC1_UNORM;
				case DXGI_FORMAT_BC2_UNORM_SRGB:
					return DXGI_FORMAT_BC2_UNORM;
				case DXGI_FORMAT_BC3_UNORM_SRGB:
					return DXGI_FORMAT_BC3_UNORM;
				case DXGI_FORMAT_BC7_UNORM_SRGB:
					return DXGI_FORMAT_BC7_UNORM;
				default:
					return format;
				}
			};

		const D3D12_RESOURCE_DESC sourceDesc = sourceResource->GetDesc();
		const D3D12_RESOURCE_DESC destinationDesc = destinationResource->GetDesc();
		const bool dimensionsMatch =
			sourceDesc.Dimension == destinationDesc.Dimension &&
			sourceDesc.Width == destinationDesc.Width &&
			sourceDesc.Height == destinationDesc.Height &&
			sourceDesc.DepthOrArraySize == destinationDesc.DepthOrArraySize &&
			sourceDesc.MipLevels == destinationDesc.MipLevels &&
			sourceDesc.SampleDesc.Count == destinationDesc.SampleDesc.Count &&
			sourceDesc.SampleDesc.Quality == destinationDesc.SampleDesc.Quality &&
			canonical_format(sourceDesc.Format) == canonical_format(destinationDesc.Format);
		if (!dimensionsMatch)
		{
			HLogError(
				"DX12CommandBuffer: cmd_copy_texture requires compatible textures, source '{}' -> destination '{}'.",
				source->get_name(),
				destination->get_name());
			return false;
		}

		if (!cmd_transition_resource_state({ source, AshResourceState::CopySrc }) ||
			!cmd_transition_resource_state({ destination, AshResourceState::CopyDst }))
		{
			return false;
		}

		const uint32_t subresourceCount =
			static_cast<uint32_t>(sourceDesc.MipLevels) *
			(sourceDesc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D ? 1u : static_cast<uint32_t>(sourceDesc.DepthOrArraySize));

		for (uint32_t subresourceIndex = 0; subresourceIndex < subresourceCount; ++subresourceIndex)
		{
			D3D12_TEXTURE_COPY_LOCATION sourceLocation{};
			sourceLocation.pResource = sourceResource;
			sourceLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
			sourceLocation.SubresourceIndex = subresourceIndex;

			D3D12_TEXTURE_COPY_LOCATION destinationLocation{};
			destinationLocation.pResource = destinationResource;
			destinationLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
			destinationLocation.SubresourceIndex = subresourceIndex;

			m_cmdList->CopyTextureRegion(&destinationLocation, 0, 0, 0, &sourceLocation, nullptr);
		}

		return true;
	}

	auto DX12CommandBuffer::cmd_update_sub_resource(std::shared_ptr<Buffer> buffer, uint32_t uOffset, uint32_t uSize, void* pData) -> bool
	{
		auto* dx12Buf = static_cast<DX12Buffer*>(buffer.get());

		// If the buffer is on upload heap, directly memcpy
		auto* mappedData = dx12Buf->get_mapped_data();
		if (mappedData)
		{
			memcpy(mappedData + uOffset, pData, uSize);
			return true;
		}

		// Otherwise use staging buffer
		if (dx12Buf->get_buffer_creation_info().access_type == AshResourceAccessType::ASH_RESOURCE_ACCESS_GPU_ONLY)
		{
			if (!cmd_transition_resource_state({ buffer, AshResourceState::CopyDst }))
			{
				return false;
			}
		}

		auto* staging = DX12Context::get()->get_staging_buffer();
		auto [stagingResource, stagingOffset] = staging->stage_data(pData, uSize);
		H_ASSERT(stagingResource);
		m_cmdList->CopyBufferRegion(
			dx12Buf->get_resource(), uOffset,
			stagingResource, stagingOffset,
			uSize);
		return true;
	}

	auto DX12CommandBuffer::cmd_update_texture_sub_resource(std::shared_ptr<Texture> texture, const void* pData) -> bool
	{
		if (!texture || !pData)
		{
			return false;
		}

		auto* dx12Texture = static_cast<DX12Texture*>(texture.get());
		ID3D12Resource* textureResource = dx12Texture->get_resource();
		if (!textureResource)
		{
			return false;
		}

		const TextureCreation& creation = dx12Texture->get_desciption();
		if (creation.eSampleCount != ASH_SAMPLE_COUNT_1_BIT)
		{
			HLogError("DX12CommandBuffer: Texture upload only supports sample count 1 for '{}'.", texture->get_name());
			return false;
		}
		if (DX12TextureFormat::is_depth_format(creation.format))
		{
			HLogError("DX12CommandBuffer: Depth/stencil texture upload is not supported for '{}'.", texture->get_name());
			return false;
		}

		const AshDXGIFormatInfo& formatInfo = get_dxgi_format_info(creation.format);
		if (formatInfo.dxgiFormat == DXGI_FORMAT_UNKNOWN ||
			formatInfo.uBytesPerBlock == 0 ||
			formatInfo.uWidthPerBlock == 0 ||
			formatInfo.uHeightPerBlock == 0)
		{
			HLogError("DX12CommandBuffer: Unsupported upload format {} for texture '{}'.", static_cast<uint32_t>(creation.format), texture->get_name());
			return false;
		}

		std::vector<TextureUploadSubresource> subresources{};
		uint64_t tightPackedBytes = 0;
		TextureUploadFormatInfo uploadFormatInfo{};
		uploadFormatInfo.bytesPerBlock = formatInfo.uBytesPerBlock;
		uploadFormatInfo.widthPerBlock = formatInfo.uWidthPerBlock;
		uploadFormatInfo.heightPerBlock = formatInfo.uHeightPerBlock;
		if (!build_tightly_packed_texture_upload_layout(creation, uploadFormatInfo, subresources, tightPackedBytes) || subresources.empty())
		{
			HLogError("DX12CommandBuffer: Failed to build upload layout for texture '{}'.", texture->get_name());
			return false;
		}

		ID3D12Device* device = DX12Context::get()->get_device();
		if (!device)
		{
			return false;
		}

		const UINT subresourceCount = static_cast<UINT>(subresources.size());
		std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> layouts(subresourceCount);
		std::vector<UINT> numRows(subresourceCount);
		std::vector<UINT64> rowSizesInBytes(subresourceCount);
		UINT64 totalUploadBytes = 0;
		const D3D12_RESOURCE_DESC textureDesc = textureResource->GetDesc();
		device->GetCopyableFootprints(
			&textureDesc,
			0,
			subresourceCount,
			0,
			layouts.data(),
			numRows.data(),
			rowSizesInBytes.data(),
			&totalUploadBytes);

		if (totalUploadBytes == 0 || totalUploadBytes > UINT32_MAX)
		{
			HLogError("DX12CommandBuffer: Texture upload footprint size is invalid for '{}'.", texture->get_name());
			return false;
		}

		std::vector<uint8_t> packedUploadData(static_cast<size_t>(totalUploadBytes), 0u);
		const uint8_t* sourceData = static_cast<const uint8_t*>(pData);
		for (UINT subresourceIndex = 0; subresourceIndex < subresourceCount; ++subresourceIndex)
		{
			const TextureUploadSubresource& subresource = subresources[subresourceIndex];
			const D3D12_PLACED_SUBRESOURCE_FOOTPRINT& layout = layouts[subresourceIndex];
			if (rowSizesInBytes[subresourceIndex] < subresource.rowBytes || numRows[subresourceIndex] < subresource.rowCount)
			{
				HLogError("DX12CommandBuffer: Texture upload footprint mismatch for '{}', subresource {}.", texture->get_name(), subresourceIndex);
				return false;
			}

			const uint64_t destinationSliceStride = static_cast<uint64_t>(layout.Footprint.RowPitch) * static_cast<uint64_t>(numRows[subresourceIndex]);
			for (uint32_t depthSlice = 0; depthSlice < subresource.depth; ++depthSlice)
			{
				const uint8_t* srcSlice = sourceData + subresource.sourceOffset + static_cast<uint64_t>(depthSlice) * subresource.sliceBytes;
				uint8_t* dstSlice = packedUploadData.data() + layout.Offset + static_cast<uint64_t>(depthSlice) * destinationSliceStride;
				for (uint32_t row = 0; row < subresource.rowCount; ++row)
				{
					std::memcpy(
						dstSlice + static_cast<uint64_t>(row) * layout.Footprint.RowPitch,
						srcSlice + static_cast<uint64_t>(row) * subresource.rowBytes,
						subresource.rowBytes);
				}
			}
		}

		auto* staging = DX12Context::get()->get_staging_buffer();
		if (!staging)
		{
			return false;
		}

		auto [stagingResource, stagingOffset] = staging->stage_data(
			packedUploadData.data(),
			static_cast<uint32_t>(totalUploadBytes),
			D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT);
		H_ASSERT(stagingResource);

		if (!cmd_transition_resource_state({ texture, AshResourceState::CopyDst }))
		{
			return false;
		}

		for (UINT subresourceIndex = 0; subresourceIndex < subresourceCount; ++subresourceIndex)
		{
			D3D12_TEXTURE_COPY_LOCATION sourceLocation{};
			sourceLocation.pResource = stagingResource;
			sourceLocation.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
			sourceLocation.PlacedFootprint = layouts[subresourceIndex];
			sourceLocation.PlacedFootprint.Offset += stagingOffset;

			D3D12_TEXTURE_COPY_LOCATION destinationLocation{};
			destinationLocation.pResource = textureResource;
			destinationLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
			destinationLocation.SubresourceIndex = subresourceIndex;

			m_cmdList->CopyTextureRegion(&destinationLocation, 0, 0, 0, &sourceLocation, nullptr);
		}

		if (creation.initial_state != AshResourceState::Unknown && creation.initial_state != AshResourceState::CopyDst)
		{
			if (!cmd_transition_resource_state({ texture, AshResourceState::CopyDst, creation.initial_state }))
			{
				return false;
			}
		}

		return true;
	}

	void DX12CommandBuffer::set_descriptor_heaps(ID3D12DescriptorHeap* cbvSrvUavHeap, ID3D12DescriptorHeap* samplerHeap)
	{
		ID3D12DescriptorHeap* heaps[] = { cbvSrvUavHeap, samplerHeap };
		UINT count = 0;
		if (cbvSrvUavHeap) count++;
		if (samplerHeap) count++;
		if (count > 0)
			m_cmdList->SetDescriptorHeaps(count, heaps);
	}
}
