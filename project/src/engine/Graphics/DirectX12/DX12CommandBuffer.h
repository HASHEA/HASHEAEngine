#pragma once
#include "DX12Wrapper.h"
#include "DX12Helper.hpp"
#include "Graphics/CommandBuffer.h"
#include <vector>

namespace RHI
{
	class DX12CommandPool;
	class DX12DescriptorHeapManager;

	class DX12CommandBuffer : public CommandBuffer
	{
	public:
		DX12CommandBuffer() = default;
		~DX12CommandBuffer();

		bool init(ID3D12Device4* device, D3D12_COMMAND_LIST_TYPE type);
		void shutdown();

		void set_allocator(ID3D12CommandAllocator* allocator) { m_currentAllocator = allocator; }
		ID3D12GraphicsCommandList4* get_command_list() const { return m_cmdList.Get(); }

	public:
		// CommandBuffer interface
		auto begin_record() -> void override;
		auto end_record() -> void override;
		auto get_state() -> AshCommandBufferState override { return m_state; }
		auto set_state(AshCommandBufferState state) -> void override { m_state = state; }
		auto get_native_handle() -> void* override { return m_cmdList.Get(); }

		auto cmd_transition_resource_state(const AshBarrier& barrierInfo) -> bool override;
		auto cmd_transition_resource_state(const std::initializer_list<AshBarrier>& lsBarrierInfoArrray) -> bool override;
		auto cmd_transition_resource_state(const AshBarrier* pBarrierInfo, uint32_t uBarrierCount) -> bool override;

		auto cmd_begin_render_pass(std::shared_ptr<Framebuffer> frameBuffer) -> void override;
		auto cmd_end_render_pass() -> void override;
		auto cmd_bind_pipeline() -> void override;
		auto cmd_set_viewport(const Viewport& viewport) -> void override;
		auto cmd_set_scissor(const Rect2DInt& scissor) -> void override;
		auto cmd_bind_vertex_buffers(uint32_t firstBinding, uint32_t bindingCount, std::shared_ptr<Buffer>* buffers, const uint64_t* offsets) -> void override;
		auto cmd_bind_index_buffer(std::shared_ptr<Buffer> buffer, uint64_t offset, AshIndexType indexType) -> void override;
		auto cmd_draw(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance) -> void override;
		auto cmd_draw_indexed(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance) -> void override;
		auto cmd_dispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) -> void override;
		auto cmd_copy_texture(std::shared_ptr<Texture> source, std::shared_ptr<Texture> destination) -> bool override;
		auto cmd_update_sub_resource(std::shared_ptr<Buffer>, uint32_t uOffset, uint32_t uSize, void* pData) -> bool override;
		auto cmd_update_texture_sub_resource(std::shared_ptr<Texture> texture, const void* pData) -> bool;

		// DX12-specific
		void set_descriptor_heaps(ID3D12DescriptorHeap* cbvSrvUavHeap, ID3D12DescriptorHeap* samplerHeap);

	private:
		void _apply_barriers(const AshBarrier* barriers, uint32_t count);

	private:
		ComPtr<ID3D12GraphicsCommandList4> m_cmdList;
		ID3D12CommandAllocator* m_currentAllocator = nullptr;
		AshCommandBufferState m_state = ASH_Idle;
		std::shared_ptr<Framebuffer> m_currentFramebuffer;
		bool m_isFirstRecord = true;
	};
}
