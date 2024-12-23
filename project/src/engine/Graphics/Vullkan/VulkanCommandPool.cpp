#include "VulkanCommandPool.h"
#include "VulkanContext.h"
namespace RHI
{
	VulkanCommandPool::VulkanCommandPool(VkDevice device, uint32_t queueFamily, VkCommandPoolCreateFlags flag, VkAllocationCallbacks* allocationCallbacks)
	{
		VkCommandPoolCreateInfo cmdPoolCI = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
		cmdPoolCI.pNext = nullptr;
		cmdPoolCI.queueFamilyIndex = queueFamily;
		cmdPoolCI.flags = flag;
		VK_CHECK_RESULT(vkCreateCommandPool(device, &cmdPoolCI, allocationCallbacks, &commandPool));
	}
	VulkanCommandPool::~VulkanCommandPool()
	{
		if (commandPool != VK_NULL_HANDLE)
		{
			vkDestroyCommandPool(VulkanContext::get_vulkan_device(), commandPool, nullptr);
		}
		
	}
	auto VulkanCommandPool::reset() -> void 
	{
		VK_CHECK_RESULT(vkResetCommandPool(VulkanContext::get_vulkan_device(), commandPool, VK_COMMAND_POOL_RESET_RELEASE_RESOURCES_BIT));
	}
};