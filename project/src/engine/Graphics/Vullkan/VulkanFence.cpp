#include "VulkanFence.h"
#include "VulkanContext.h"
namespace RHI
{
	VulkanFence::VulkanFence(bool createSignaled) : signaled(createSignaled)
	{
		VkFenceCreateInfo fenceCreateInfo{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
		fenceCreateInfo.flags = createSignaled ? VK_FENCE_CREATE_SIGNALED_BIT : 0;
		vkCreateFence(VulkanContext::get_vulkan_device(), &fenceCreateInfo, nullptr, &fence);
	}
	VulkanFence::~VulkanFence()
	{
		vkDestroyFence(VulkanContext::get_vulkan_device(), fence, nullptr);
	}
	auto VulkanFence::reset() -> void
	{
		if (signaled)
			VK_CHECK_RESULT(vkResetFences(VulkanContext::get_vulkan_device(), 1, &fence));
		signaled = false;
	}
	auto VulkanFence::wait() -> bool
	{
		H_ASSERTLOG(!signaled, "Fence Signaled");

		const VkResult result = vkWaitForFences(VulkanContext::get_vulkan_device(), 1, &fence, true, UINT32_MAX);

		//VK_CHECK_RESULT(result);
		if (result == VK_SUCCESS)
		{
			signaled = true;
			return false;
		}
		return true;
	}
	auto VulkanFence::wait_and_reset() -> void
	{
		if (!is_signaled())
			wait();
		reset();
	}
	auto VulkanFence::check_state() -> bool
	{

		H_ASSERTLOG(!signaled, "Fence Signaled");

		const VkResult result = vkGetFenceStatus(VulkanContext::get_vulkan_device(), fence);
		if (result == VK_SUCCESS)
		{
			signaled = true;
			return true;
		}

		return false;
	}
	auto VulkanFence::is_signaled() -> bool
	{
		if (signaled)
		{
			return true;
		}
		else
		{
			return check_state();
		}
	}
};