#include "DX12CommandBuffer.h"
#include "DX12Context.h"
#include "DX12Buffer.h"
#include "DX12Texture.h"
#include "DX12Framebuffer.h"
#include "DX12RenderPass.h"
#include "DX12StagingBuffer.h"
#include "Base/hlog.h"
#include "Base/hassert.h"
#include "Graphics/Framebuffer.h"

namespace RHI
{
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

				if (barrier.eSRCAccess == AshResourceState::Unknown)
					stateBefore = ash_to_d3d12_resource_state(dx12Tex->get_resource_state());

				if (stateBefore == stateAfter) continue;

				d3dBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
				d3dBarrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
				d3dBarrier.Transition.pResource = resource;
				d3dBarrier.Transition.StateBefore = stateBefore;
				d3dBarrier.Transition.StateAfter = stateAfter;
				d3dBarrier.Transition.Subresource = barrier.IsWholeResource() ?
					D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES : barrier.uBaseMipLevel;

				dx12Tex->set_resource_state(barrier.eDSTAccess);
				d3dBarriers.push_back(d3dBarrier);
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
		auto* staging = DX12Context::get()->get_staging_buffer();
		auto [stagingResource, stagingOffset] = staging->stage_data(pData, uSize);
		if (stagingResource)
		{
			m_cmdList->CopyBufferRegion(
				dx12Buf->get_resource(), uOffset,
				stagingResource, stagingOffset,
				uSize);
			return true;
		}

		return false;
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
