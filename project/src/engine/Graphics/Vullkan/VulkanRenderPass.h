#pragma once
#include "Graphics/RenderPass.h"
namespace RHI
{
	class VulkanRenderPass : public RenderPass
	{
	public:
		VulkanRenderPass(const RenderPassCreation& ci);
		~VulkanRenderPass();

	private:
		VkRenderPass vkRenderPass = VK_NULL_HANDLE;
		const char* name = nullptr;
	};

	
}