#include "VulkanCommandPool.h"
namespace RHI
{
	VulkanCommandPool::VulkanCommandPool(VkDevice device, uint32_t queueFamily, VkCommandPoolCreateFlags flag, VkAllocationCallbacks* allocationCallbacks)
	{
		VkCommandPoolCreateInfo cmdPoolCI = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
		cmdPoolCI.pNext = nullptr;
		cmdPoolCI.queueFamilyIndex = queueFamily;
		cmdPoolCI.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
		VK_CHECK_RESULT(vkCreateCommandPool(device, &cmdPoolCI, allocationCallbacks, &commandPool));
	}
	VulkanCommandPool::~VulkanCommandPool()
	{
	}
	auto VulkanCommandPool::reset() -> void 
	{
		
	}
};