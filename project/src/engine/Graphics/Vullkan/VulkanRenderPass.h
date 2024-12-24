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
	private:
		VkRenderPass vkRenderPass = VK_NULL_HANDLE;
		const char* name = nullptr;

		
	};

	
}