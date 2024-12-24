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
		Array<VkImageView> attachments;
		attachments.init(nullptr, ci.colorAttachments.size());
		attachmentsRef.init(nullptr, ci.colorAttachments.size());
		auto count = ci.colorAttachments.size();
		for (size_t i = 0; i < count; i++)
		{
			attachments.push_back((VkImageView)ci.colorAttachments[i]->get_native_handle());
			attachmentsRef.push_back(ci.colorAttachments[i]);
		}
		if (ci.depthStencilAttachment != nullptr)
		{
			attachments.push_back((VkImageView)ci.depthStencilAttachment->get_native_handle());
			attachmentsRef.push_back(ci.depthStencilAttachment);
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
		VK_CHECK_RESULT(vkCreateFramebuffer(VulkanContext::get_vulkan_device(),&framebuffer_info,VulkanContext::get_vulkan_allocation_callbacks(),&vkFramebuffer));
		VulkanContext::set_resource_name(VK_OBJECT_TYPE_FRAMEBUFFER, (uint64_t)vkFramebuffer,name);
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
		attachmentsRef.shutdown();
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
}