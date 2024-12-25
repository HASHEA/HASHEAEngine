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
		/*rhi interfaces*/
	private:

	};
}