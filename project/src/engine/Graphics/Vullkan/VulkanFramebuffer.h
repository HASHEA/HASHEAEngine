#pragma once
#include "Graphics/Framebuffer.h"
#include "VulkanHelper.hpp"
namespace RHI
{
	class VulkanFramebuffer : public Framebuffer	
	{
	public:
		VulkanFramebuffer(const FramebufferCreation& ci);
		~VulkanFramebuffer();
		static auto create(const FramebufferCreation& ci) -> std::shared_ptr<VulkanFramebuffer>;
	public:
		auto get_native_handle() -> void* override;
		auto get_name() -> const char* override;
		/*rhi interfaces*/
		auto get_render_pass() -> std::shared_ptr<RenderPass> override;
		auto get_render_targets() -> Array<std::shared_ptr<Texture>> & override;
		auto get_depth_stencil() -> std::shared_ptr<Texture> override;
		auto clear_render_target(uint32_t index, const AshColorValue& color) -> void override;
		auto clear_depth_stencil(const AshDepthStencilValue& color) -> void override;
		auto get_render_target_clear_color(uint32_t index) -> const AshColorValue& override;
		auto get_depth_stencil_clear_color() -> const AshDepthStencilValue & override;
		auto get_width() -> uint32_t override;
		auto get_height() -> uint32_t override;
		auto get_layer_count() -> uint32_t override;
		auto get_shading_rate_attachment() -> std::shared_ptr<Texture> override;
		/*rhi interfaces*/

	private:
		VkFramebuffer vkFramebuffer = VK_NULL_HANDLE;
		std::shared_ptr<RenderPass> renderPass = nullptr;
		const char* name = nullptr;
		uint16_t width = 0;
		uint16_t height = 0;
		uint16_t layers = 0;
		//hold ref of attachments:
		Array<std::shared_ptr<Texture>> colorAttachements;
		std::shared_ptr<Texture> depthStencilAttachment = nullptr;
		std::shared_ptr<Texture> shadingRateAttachment = nullptr;
		Array<AshColorValue> colorClearColors;
		AshDepthStencilValue depthClearColors{};

		

		


		

		

	};
}