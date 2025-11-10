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
		virtual auto cmd_update_sub_resource(std::shared_ptr<Buffer>, uint32_t uOffset, uint32_t uSize, void* pData) -> bool = 0;
	private:

	};
};