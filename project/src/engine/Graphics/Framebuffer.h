#pragma once
#include "RHIResource.h"
#include "Base/ds/harray.hpp"
using namespace AshEngine;
namespace RHI
{
	struct FramebufferCreation
	{
		std::shared_ptr<RenderPass> renderPass = nullptr;
		Array<std::shared_ptr<TextureView>> colorAttachments;
		std::shared_ptr<TextureView> depthStencilAttachment = nullptr;

		uint16_t width = 0;
		uint16_t height = 0;
		uint16_t layers = 0;
		const char* name = nullptr;
	};
	class Framebuffer : public RHIResource
	{
	public:
		Framebuffer() = default;
		virtual ~Framebuffer() {}

	private:

	};

}