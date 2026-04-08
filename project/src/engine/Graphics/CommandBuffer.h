#pragma once
#include "RHICommon.h"
namespace RHI
{
	struct TextureSubResource;
	class Texture;
	class Buffer;
	class Framebuffer;
	enum class AshResourceState : uint32_t;
	struct AshBarrier;
	class CommandBuffer
	{
	public:
		CommandBuffer() = default;
		virtual ~CommandBuffer() {}
	public:
	
		virtual auto begin_record() -> void = 0;
		virtual auto end_record() ->  void = 0;
		virtual auto get_state()->AshCommandBufferState = 0;
		virtual auto set_state(AshCommandBufferState state) ->void = 0;
		
	
		virtual auto get_native_handle() -> void* = 0;
	public:

		virtual auto cmd_transition_resource_state(const AshBarrier& barrierInfo) -> bool = 0;
		virtual auto cmd_transition_resource_state(const std::initializer_list<AshBarrier>& lsBarrierInfoArrray) -> bool = 0;
		virtual auto cmd_transition_resource_state(const AshBarrier* pBarrierInfo, uint32_t uBarrierCount) -> bool = 0;

		virtual auto cmd_begin_render_pass(std::shared_ptr<Framebuffer> frameBuffer) -> void = 0;
		virtual auto cmd_end_render_pass() -> void = 0;
		virtual auto cmd_bind_pipeline() -> void = 0;
		virtual auto cmd_set_viewport(const Viewport& viewport) -> void = 0;
		virtual auto cmd_set_scissor(const Rect2DInt& scissor) -> void = 0;
		virtual auto cmd_bind_vertex_buffers(uint32_t firstBinding, uint32_t bindingCount, std::shared_ptr<Buffer>* buffers, const uint64_t* offsets) -> void = 0;
		virtual auto cmd_bind_index_buffer(std::shared_ptr<Buffer> buffer, uint64_t offset, AshIndexType indexType) -> void = 0;
		virtual auto cmd_draw(uint32_t vertexCount, uint32_t instanceCount = 1, uint32_t firstVertex = 0, uint32_t firstInstance = 0) -> void = 0;
		virtual auto cmd_draw_indexed(uint32_t indexCount, uint32_t instanceCount = 1, uint32_t firstIndex = 0, int32_t vertexOffset = 0, uint32_t firstInstance = 0) -> void = 0;
		virtual auto cmd_dispatch(uint32_t groupCountX, uint32_t groupCountY = 1, uint32_t groupCountZ = 1) -> void = 0;
		virtual auto cmd_update_sub_resource(std::shared_ptr<Buffer>, uint32_t uOffset, uint32_t uSize, void* pData) -> bool = 0;
	private:

	};
};
