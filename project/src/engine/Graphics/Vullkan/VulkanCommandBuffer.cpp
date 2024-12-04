#include "VulkanCommandPool.h"
#include "VulkanCommandBuffer.h"
#include "VulkanContext.h"
namespace RHI
{
	auto VulkanCommandBufferManager::init(uint32_t numThread) -> void
	{
		numPoolsPerFrame = numThread;
		const uint32_t totalPools = numPoolsPerFrame * k_max_frames;
		usedBuffers.init(nullptr, totalPools, totalPools);
		usedSecondaryCommandBuffers.init(nullptr, totalPools);
		const uint32_t totalBuffers = totalPools * numCommandBuffersPerThread;
		commandBuffers.init(nullptr, totalBuffers, totalBuffers);
		const uint32_t totalSecondaryBuffers = totalPools * k_secondary_command_buffers_count;
		secondaryCommandBuffers.init(nullptr, totalSecondaryBuffers,totalSecondaryBuffers);
		for (uint32_t i = 0; i < totalBuffers; i++)
		{
			VkCommandBufferAllocateInfo cmd = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
			cmd.pNext = nullptr;
			const uint32_t frameIndex = i / (numCommandBuffersPerThread * numPoolsPerFrame);
			const uint32_t threadIndex = (i / numCommandBuffersPerThread) % numPoolsPerFrame;
			const uint32_t poolIndex = pool_from_indices(frameIndex, threadIndex);
			cmd.commandPool = VulkanContext::get_frame_pool(poolIndex).cmdPool->get_handle();
			cmd.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
			cmd.commandBufferCount = 1;
			VulkanCommandBuffer& cmdBuffer = commandBuffers[i];
			cmdBuffer.allocate_vulkan_command_buffer(&cmd);
			cmdBuffer.init(i, &VulkanContext::get_frame_pool(poolIndex));
		}
		uint32_t index = totalBuffers;
		for (uint32_t poolIndex = 0; poolIndex < totalPools; poolIndex++)
		{
			VkCommandBufferAllocateInfo cmd = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
			cmd.pNext = nullptr;
			cmd.commandPool = VulkanContext::get_frame_pool(poolIndex).cmdPool->get_handle();
			cmd.level = VK_COMMAND_BUFFER_LEVEL_SECONDARY;
			cmd.commandBufferCount = 1;
			for (uint32_t scbIndex = 0; scbIndex < k_secondary_command_buffers_count; scbIndex++)
			{
				VulkanCommandBuffer& scb = secondaryCommandBuffers[index - totalBuffers];
				scb.allocate_vulkan_command_buffer(&cmd);
				scb.init(index++, &VulkanContext::get_frame_pool(poolIndex));
			}
		}
	}
	auto VulkanCommandBufferManager::shutdown() -> void
	{
		for (uint32_t i = 0; i < commandBuffers.size(); i++) {
			commandBuffers[i].shutdown();
		}
		commandBuffers.shutdown();
		for (uint32_t i = 0; i < secondaryCommandBuffers.size(); ++i) {
			secondaryCommandBuffers[i].shutdown();
		}
		secondaryCommandBuffers.shutdown();
		usedBuffers.shutdown();
		usedSecondaryCommandBuffers.shutdown();
	}
	auto VulkanCommandBufferManager::reset_pools(uint32_t frameIndex) -> void
	{
	}
	auto VulkanCommandBufferManager::get_command_buffer(uint32_t frameIndex, uint32_t threadIndex, bool begin) -> VulkanCommandBuffer*
	{
		return nullptr;
	}
	auto VulkanCommandBufferManager::get_secondary_command_buffer(uint32_t frameIndex, uint32_t threadIndex) -> VulkanCommandBuffer*
	{
		return nullptr;
	}
	uint32_t VulkanCommandBufferManager::pool_from_indices(uint32_t frame_index, uint32_t thread_index)
	{
		return frame_index * numPoolsPerFrame + thread_index;
	}
	auto VulkanCommandBuffer::init(uint32_t index,const FramePool* framePool) -> void
	{
	}
	auto VulkanCommandBuffer::shutdown() -> void
	{
	}
	auto VulkanCommandBuffer::allocate_vulkan_command_buffer(const VkCommandBufferAllocateInfo* const cmd) -> void
	{
		VK_CHECK_RESULT(vkAllocateCommandBuffers(VulkanContext::get_vulkan_device(), cmd, &commandBuffer));
	}
}