#include "VulkanFramebuffer.h"
#include "VulkanRenderPass.h"
#include "VulkanContext.h"
namespace RHI
{
	VulkanFramebuffer::VulkanFramebuffer(const FramebufferCreation& ci)
	{
		name = ci.name;
		width = ci.width;
		height = ci.height;
		layers = ci.layers;
		renderPass = ci.renderPass;
		Array<VkImageView> attachments;
		attachments.init(nullptr, ci.colorAttachments.size());
		colorAttachements.init(nullptr, ci.colorAttachments.size());
		colorClearColors.init(nullptr, ci.colorAttachments.size());
		auto count = ci.colorAttachments.size();
		for (size_t i = 0; i < count; i++)
		{
			attachments.push_back((VkImageView)ci.colorAttachments[i]->get_default_render_target_view()->get_native_handle());
			colorAttachements.push_back(ci.colorAttachments[i]);
			colorClearColors.push_back(AshColorValue());
		}
		if (ci.depthStencilAttachment != nullptr)
		{
			attachments.push_back((VkImageView)ci.depthStencilAttachment->get_default_render_target_view()->get_native_handle());
			depthStencilAttachment = ci.depthStencilAttachment;
		}
		shadingRateAttachment = ci.shadingRateAttachment;
		if (!VulkanContext::get()->get_device_extension_enabled(DeviceExtensionAndFeaturesFlags::DynamicRendering))
		{
			if (shadingRateAttachment != nullptr)
			{
				HLogWarning("fragment shading rate is not supported in non dynamic rendering mode !");
			}
			
			VkFramebufferCreateInfo framebuffer_info{ VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
			framebuffer_info.pNext = nullptr;
			framebuffer_info.renderPass = (VkRenderPass)ci.renderPass->get_native_handle();
			framebuffer_info.width = ci.width;
			framebuffer_info.height = ci.height;
			framebuffer_info.layers = ci.layers;
			framebuffer_info.pAttachments = attachments.m_pData;
			framebuffer_info.attachmentCount = count;
			HLogInfo("creating frame buffer : {} ...", name);
			VK_CHECK_RESULT(vkCreateFramebuffer(VulkanContext::get_vulkan_device(), &framebuffer_info, VulkanContext::get_vulkan_allocation_callbacks(), &vkFramebuffer));
			VulkanContext::set_resource_name(VK_OBJECT_TYPE_FRAMEBUFFER, (uint64_t)vkFramebuffer, name);
		}	
		attachments.shutdown();
	}
	VulkanFramebuffer::~VulkanFramebuffer()
	{
		if (immediate_deletion)
		{
			HLogInfo("deleting frame buffer : {}", name);
			vkDestroyFramebuffer(VulkanContext::get_vulkan_device(),vkFramebuffer,VulkanContext::get_vulkan_allocation_callbacks());
			
		}
		else
		{
			auto handle = this->vkFramebuffer;
			auto sname = name;
			if (handle != VK_NULL_HANDLE)
			{
				VulkanContext::get_current_frame_deletion_queue().emplace([handle, sname]() {
					HLogInfo("deleting frame buffer : {} ...", sname);
					vkDestroyFramebuffer(VulkanContext::get_vulkan_device(), handle, VulkanContext::get_vulkan_allocation_callbacks()); 
					});
			}
		}
		//release ref of views after destroy framebuffers
		colorAttachements.shutdown();
		colorClearColors.shutdown();
	}
	auto VulkanFramebuffer::create(const FramebufferCreation& ci) -> std::shared_ptr<VulkanFramebuffer>
	{
		return Ash_New_Shared<VulkanFramebuffer>(ci);
	}
	auto VulkanFramebuffer::get_native_handle() -> void*
	{
		return vkFramebuffer;
	}
	auto VulkanFramebuffer::get_name() -> const char*
	{
		return name;
	}
	auto VulkanFramebuffer::get_render_pass() -> std::shared_ptr<RenderPass>
	{
		return renderPass;
	}
	auto VulkanFramebuffer::get_render_targets() -> Array<std::shared_ptr<Texture>>&
	{
		return colorAttachements;
	}
	auto VulkanFramebuffer::get_depth_stencil() -> std::shared_ptr<Texture>
	{
		return depthStencilAttachment;
	}
	auto VulkanFramebuffer::clear_render_target(uint32_t index, const AshColorValue& color) -> void
	{
		if (renderPass->get_color_operations()[index] != AshLoadOption::ASH_LOAD_CLEAR)
		{
			HLogWarning("try to clear render target without load option [clear] ! do nothing and return !");
			return;
		}
		colorClearColors[index] = color;
	}
	auto VulkanFramebuffer::clear_depth_stencil(const AshDepthStencilValue& color) -> void
	{
		if (renderPass->get_depth_stencil_operations() != AshLoadOption::ASH_LOAD_CLEAR)
		{
			HLogWarning("try to clear depth stencil without load option [clear] ! do nothing and return !");
			return;
		}
		depthClearColors = color;
	}
	auto VulkanFramebuffer::get_render_target_clear_color(uint32_t index) -> const AshColorValue&
	{
		return colorClearColors[index];
	}
	auto VulkanFramebuffer::get_depth_stencil_clear_color() -> const AshDepthStencilValue&
	{
		return depthClearColors;
	}
	auto VulkanFramebuffer::get_width() -> uint32_t
	{
		return width;
	}
	auto VulkanFramebuffer::get_height() -> uint32_t
	{
		return height;
	}
	auto VulkanFramebuffer::get_layer_count() -> uint32_t
	{
		return layers;
	}
	auto VulkanFramebuffer::get_shading_rate_attachment() -> std::shared_ptr<Texture>
	{
		return shadingRateAttachment;
	}
}