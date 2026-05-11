#pragma once
#include "Graphics/CommandPool.h"
#include "VulkanHelper.hpp"
#include "VulkanWrapper.h"
namespace RHI
{
	class VulkanCommandPool : public CommandPool
	{
	public:
		VulkanCommandPool(VkDevice device,uint32_t queueFamily, VkCommandPoolCreateFlags flag, VkAllocationCallbacks* allocationCallbacks = nullptr);
		~VulkanCommandPool();
		NO_COPYABLE(VulkanCommandPool);
		virtual auto reset() -> void override;
	public:
		auto get_handle() -> VkCommandPool { return commandPool; }
	private:
		VkCommandPool commandPool = VK_NULL_HANDLE;
	};
};