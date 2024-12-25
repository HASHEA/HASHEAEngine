#pragma once
#include "RHICommon.h"
namespace RHI
{
	struct TextureSubResource;
	class Texture;
	class CommandBuffer
	{
	public:
		CommandBuffer() = default;
		virtual ~CommandBuffer() {}
	public:
		//rhi interfaces
		virtual auto begin() -> void = 0;
		virtual auto end() ->  void = 0;
		virtual auto get_state()->AshCommandBufferState = 0;
		virtual auto set_state(AshCommandBufferState state) ->void = 0;
		virtual auto transition_image_state(std::shared_ptr<Texture> texture, AshResourceState newlayout, 
			TextureSubResource* region = nullptr, AshQueueType::Enum srcQueueType = AshQueueType::Enum::Ignored, AshQueueType::Enum dstQueueType = AshQueueType::Enum::Ignored) -> void = 0;
		virtual auto begin_render_pass(std::shared_ptr<Framebuffer> frameBuffer) -> void = 0;
		virtual auto end_render_pass() -> void = 0;
		virtual auto bind_pipeline() -> void = 0;
		virtual auto get_native_handle() -> void* = 0;
	private:

	};
};