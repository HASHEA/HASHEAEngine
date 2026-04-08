#pragma once
#include "Graphics/RenderPass.h"
#include <memory>
namespace RHI
{
	class VulkanRenderPass : public RenderPass
	{
	public:
		VulkanRenderPass(const RenderPassCreation& ci);
		~VulkanRenderPass();
	public:
		static auto create(const RenderPassCreation& ci) -> std::shared_ptr<VulkanRenderPass>;
		auto get_native_handle() -> void* override;
		auto get_name() -> const char* override;
		auto get_color_operations() -> const Array<AshLoadOption> & override;
		auto get_color_attachment_count() -> uint32_t override;
		auto get_color_attachment_format(uint32_t index) -> AshFormat override;

		auto get_depth_stencil_operations() -> AshLoadOption override;

		auto get_depth_stencil_format() -> AshFormat override;

		auto get_multiview_mask() -> uint32_t override;

		auto get_color_attachment_final_state(uint32_t index) -> AshResourceState override;

		auto get_depth_stencil_attachment_final_state() -> AshResourceState override;
	private:
		VkRenderPass vkRenderPass = VK_NULL_HANDLE;
		const char* name = nullptr;
		Array<AshLoadOption> colorLoadOptions;
		AshFormat colorFormats[k_max_image_outputs]{};
		uint32_t colorAttachmentCount = 0;
		AshLoadOption depthStencilLoadOption = AshLoadOption::ASH_LOAD_DONT_CARE;
		AshFormat depthStencilFormat = ASH_FORMAT_UNDEFINED;
		uint32_t multiviewMask = 0;
		AshResourceState                color_final_layouts[k_max_image_outputs];
		AshResourceState                depth_stencil_final_layout;
	};

	
}
