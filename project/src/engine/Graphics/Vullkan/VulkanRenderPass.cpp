#include "VulkanHelper.hpp"
#include "VulkanRenderPass.h"
#include "VulkanContext.h"
namespace RHI
{
	VulkanRenderPass::VulkanRenderPass(const RenderPassCreation& ci)
	{
		name = ci.name;
		VkAttachmentDescription color_attachments[8] = {};
		VkAttachmentReference color_attachments_ref[8] = {};
		VkAttachmentLoadOp depth_op, stencil_op;
		VkImageLayout depth_initial;
		switch (ci.depth_operation) 
		{
		case AshLoadOption::ASH_LOAD_LOAD :
			depth_op = VK_ATTACHMENT_LOAD_OP_LOAD;
			depth_initial = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
			break;
		case AshLoadOption::ASH_LOAD_CLEAR:
			depth_op = VK_ATTACHMENT_LOAD_OP_CLEAR;
			depth_initial = VK_IMAGE_LAYOUT_UNDEFINED;
			break;
		default:
			depth_op = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			depth_initial = VK_IMAGE_LAYOUT_UNDEFINED;
			break;
		}
		switch (ci.stencil_operation) {
		case AshLoadOption::ASH_LOAD_LOAD:
			stencil_op = VK_ATTACHMENT_LOAD_OP_LOAD;
			break;
		case AshLoadOption::ASH_LOAD_CLEAR:
			stencil_op = VK_ATTACHMENT_LOAD_OP_CLEAR;
			break;
		default:
			stencil_op = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			break;
		}
		// Color attachments
		uint32_t c = 0;
		for (; c < ci.num_render_targets; ++c) 
		{
			VkAttachmentLoadOp color_op;
			VkImageLayout color_initial;
			switch (ci.color_operations[c]) 
			{
			case AshLoadOption::ASH_LOAD_LOAD:
				color_op = VK_ATTACHMENT_LOAD_OP_LOAD;
				color_initial = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
				break;
			case AshLoadOption::ASH_LOAD_CLEAR:
				color_op = VK_ATTACHMENT_LOAD_OP_CLEAR;
				color_initial = VK_IMAGE_LAYOUT_UNDEFINED;
				break;
			default:
				color_op = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
				color_initial = VK_IMAGE_LAYOUT_UNDEFINED;
				break;
			}
			VkAttachmentDescription& color_attachment = color_attachments[c];
			color_attachment.format = get_vk_texture_format_info(ci.color_formats[c]).vkFormat;
			color_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
			color_attachment.loadOp = color_op;
			color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
			color_attachment.stencilLoadOp = stencil_op;
			color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			color_attachment.initialLayout = color_initial;
			color_attachment.finalLayout = ash_resource_state_to_vk_image_layout(ci.color_final_layouts[c]);
			VkAttachmentReference& color_attachment_ref = color_attachments_ref[c];
			color_attachment_ref.attachment = c;
			color_attachment_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			color_final_layouts[c] = ci.color_final_layouts[c];
		}
		// Depth attachment
		VkAttachmentDescription depth_attachment{};
		VkAttachmentReference depth_attachment_ref{};
		if (ci.depth_stencil_format != VK_FORMAT_UNDEFINED)
		{
			depth_attachment.format = get_vk_texture_format_info(ci.depth_stencil_format).vkFormat;
			depth_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
			depth_attachment.loadOp = depth_op;
			depth_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
			depth_attachment.stencilLoadOp = stencil_op;
			depth_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			depth_attachment.initialLayout = depth_initial;
			depth_attachment.finalLayout = ash_resource_state_to_vk_image_layout(ci.depth_stencil_final_layout);
			depth_attachment_ref.attachment = c;
			depth_attachment_ref.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
			depth_stencil_final_layout = ci.depth_stencil_final_layout;
		}
		// Create subpass.
		// TODO: for now is just a simple subpass, evolve API.
		VkSubpassDescription subpass = {};
		subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;

		// Calculate active attachments for the subpass
		VkAttachmentDescription attachments[k_max_image_outputs + 1]{};
		for (uint32_t active_attachments = 0; active_attachments < ci.num_render_targets; ++active_attachments) {
			attachments[active_attachments] = color_attachments[active_attachments];
		}
		subpass.colorAttachmentCount = ci.num_render_targets;
		subpass.pColorAttachments = color_attachments_ref;

		subpass.pDepthStencilAttachment = nullptr;

		uint32_t depth_stencil_count = 0;
		if (get_vk_texture_format_info(ci.depth_stencil_format).vkFormat != VK_FORMAT_UNDEFINED) {
			attachments[subpass.colorAttachmentCount] = depth_attachment;
			subpass.pDepthStencilAttachment = &depth_attachment_ref;
			depth_stencil_count = 1;
		}

		VkRenderPassCreateInfo render_pass_info = { VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };

		render_pass_info.attachmentCount = (ci.num_render_targets) + depth_stencil_count;
		render_pass_info.pAttachments = attachments;
		render_pass_info.subpassCount = 1;
		render_pass_info.pSubpasses = &subpass;

		// Create external subpass dependencies
		//VkSubpassDependency external_dependencies[ 16 ];
		//u32 num_external_dependencies = 0;
		VkRenderPassMultiviewCreateInfo multiview_create_info{ VK_STRUCTURE_TYPE_RENDER_PASS_MULTIVIEW_CREATE_INFO };
		if (ci.multiview_mask > 0 && VulkanContext::get()->get_device_extension_enabled(DeviceExtensionAndFeaturesFlags::Multiview)) {
			multiviewMask = ci.multiview_mask;
			multiview_create_info.subpassCount = 1;
			multiview_create_info.pViewMasks = &ci.multiview_mask;
			multiview_create_info.correlationMaskCount = 0;
			multiview_create_info.pCorrelationMasks = nullptr;
			render_pass_info.pNext = &multiview_create_info;
		}
	
		if (!VulkanContext::get()->get_device_extension_enabled(DeviceExtensionAndFeaturesFlags::DynamicRendering))
		{
			HLogInfo("creating render pass : {}", name);
			VK_CHECK_RESULT(vkCreateRenderPass(VulkanContext::get_vulkan_device(), &render_pass_info, nullptr, &vkRenderPass));
			VulkanContext::set_resource_name(VK_OBJECT_TYPE_RENDER_PASS, (uint64_t)vkRenderPass, name);
		}
	}
	VulkanRenderPass::~VulkanRenderPass()
	{
		if (immediate_deletion)
		{
			if (vkRenderPass != VK_NULL_HANDLE)
			{
				HLogInfo("deleting renderpass : {} ...", name);
				vkDestroyRenderPass(VulkanContext::get_vulkan_device(),vkRenderPass,VulkanContext::get_vulkan_allocation_callbacks());
			}
		}
		else
		{
			auto handle = this->vkRenderPass;
			auto sname = name;
			if (handle != VK_NULL_HANDLE)
			{
				VulkanContext::get_current_frame_deletion_queue().emplace([handle, sname]() {
					HLogInfo("deleting renderpass : {} ...", sname);
					vkDestroyRenderPass(VulkanContext::get_vulkan_device(), handle, VulkanContext::get_vulkan_allocation_callbacks()); });
			}
		}
	}
	auto VulkanRenderPass::create(const RenderPassCreation& ci) -> std::shared_ptr<VulkanRenderPass>
	{
		return Ash_New_Shared<VulkanRenderPass>(ci);
	}
	auto VulkanRenderPass::get_native_handle() -> void*
	{
		return vkRenderPass;
	}
	auto VulkanRenderPass::get_name() -> const char*
	{
		return name;
	}
	auto VulkanRenderPass::get_color_operations() -> const Array<AshLoadOption>&
	{
		return colorLoadOptions;
	}
	auto VulkanRenderPass::get_depth_stencil_operations() -> AshLoadOption
	{
		return depthStencilLoadOption;
	}
	auto VulkanRenderPass::get_depth_stencil_format() -> AshFormat
	{
		return depthStencilFormat;
	}
	auto VulkanRenderPass::get_multiview_mask() -> uint32_t
	{
		return multiviewMask;
	}
	auto VulkanRenderPass::get_color_attachment_final_state(uint32_t index) -> AshResourceState
	{
		return color_final_layouts[index];
	}
	auto VulkanRenderPass::get_depth_stencil_attachment_final_state() -> AshResourceState
	{
		return depth_stencil_final_layout;
	}
}