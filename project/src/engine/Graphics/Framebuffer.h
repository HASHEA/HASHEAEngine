#pragma once
#include "RHIResource.h"
#include "Base/ds/harray.hpp"
using namespace AshEngine;
namespace RHI
{
	struct FramebufferCreation
	{
		std::shared_ptr<RenderPass> renderPass = nullptr;
		Array<std::shared_ptr<Texture>> colorAttachments;
		std::shared_ptr<Texture> depthStencilAttachment = nullptr;
		std::shared_ptr<Texture> shadingRateAttachment = nullptr;
		uint16_t width = 0;
		uint16_t height = 0;
		uint16_t layers = 0;
		const char* name = nullptr;
	};
	class RenderPass;
	class Framebuffer : public RHIResource
	{
	public:
		Framebuffer() = default;
		virtual ~Framebuffer() {}
	public:
		/*rhi interfaces*/
		virtual auto get_render_pass() -> std::shared_ptr<RenderPass> = 0;
		virtual auto get_render_targets() -> Array<std::shared_ptr<Texture>>& = 0;
		virtual auto get_depth_stencil() -> std::shared_ptr<Texture> = 0;
		virtual auto get_shading_rate_attachment() -> std::shared_ptr<Texture> = 0;
		virtual auto get_render_target_clear_color(uint32_t index) -> const AshColorValue& = 0;
		virtual auto get_depth_stencil_clear_color() -> const AshDepthStencilValue & = 0;
		//call before begin
		virtual auto clear_render_target(uint32_t index, const AshColorValue& color) -> void = 0;
		virtual auto clear_depth_stencil(const AshDepthStencilValue& color) -> void = 0;
		virtual auto get_width() -> uint32_t = 0;
		virtual auto get_height() -> uint32_t = 0;
		virtual auto get_layer_count() -> uint32_t = 0;
		/*rhi interfaces*/
	private:

	};
}
