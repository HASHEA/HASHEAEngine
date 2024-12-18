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
			const uint32_t frameIndex = i / (numCommandBuffersPerThread * numPoolsPerFrame);
			const uint32_t threadIndex = (i / numCommandBuffersPerThread) % numPoolsPerFrame;
			const uint32_t poolIndex = pool_from_indices(frameIndex, threadIndex);
			VulkanCommandBuffer& cmdBuffer = commandBuffers[i];
			cmdBuffer.init(i,poolIndex,false);
		}
		uint32_t index = totalBuffers;
		for (uint32_t poolIndex = 0; poolIndex < totalPools; poolIndex++)
		{
			for (uint32_t scbIndex = 0; scbIndex < k_secondary_command_buffers_count; scbIndex++)
			{
				VulkanCommandBuffer& scb = secondaryCommandBuffers[index - totalBuffers];
				scb.init(index,poolIndex,true);
				index++;
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
	auto VulkanCommandBufferManager::get_command_buffer(uint32_t frameIndex, uint32_t threadIndex) -> VulkanCommandBuffer
	{
		auto poolIndex = pool_from_indices(frameIndex,threadIndex);
		auto& curCount = usedBuffers[poolIndex];
		H_ASSERTLOG(curCount <= numCommandBuffersPerThread, "used commandbuffer count exceed max count per thread {0} > {1}", curCount,numCommandBuffersPerThread);
		curCount++;
		VulkanCommandBuffer ret = commandBuffers[frameIndex];
		commandBuffers.delete_swap(frameIndex);
		return ret;
	}
	auto VulkanCommandBufferManager::get_secondary_command_buffer(uint32_t frameIndex, uint32_t threadIndex) -> VulkanCommandBuffer
	{
		return {};
	}
	uint32_t VulkanCommandBufferManager::pool_from_indices(uint32_t frame_index, uint32_t thread_index)
	{
		return frame_index * numPoolsPerFrame + thread_index;
	}
	auto VulkanCommandBuffer::init(uint32_t allocIndex, uint32_t poolIndex, bool _secondary) -> void
	{
		this->index = allocIndex;
		this->secondary = _secondary;
		commandPool = VulkanContext::get_frame_pool(poolIndex).cmdPool->get_handle();
		VkCommandBufferAllocateInfo cmd = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
		cmd.pNext = nullptr;
		cmd.commandPool = commandPool;
		cmd.level = _secondary ? VK_COMMAND_BUFFER_LEVEL_SECONDARY : VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		cmd.commandBufferCount = 1;
		VK_CHECK_RESULT(vkAllocateCommandBuffers(VulkanContext::get_vulkan_device(), &cmd, &commandBuffer));
	}
	auto VulkanCommandBuffer::shutdown() -> void
	{
		if (commandBuffer)
			vkFreeCommandBuffers(VulkanContext::get_vulkan_device(), commandPool,1, &commandBuffer);
	}

	auto VulkanCommandBuffer::begin() -> void
	{
		if (secondary)
		{
			/*VkCommandBufferInheritanceInfo inheritanceInfo{};
			inheritanceInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO;
			inheritanceInfo.subpass = 0;
			inheritanceInfo.renderPass = *(VulkanRenderPass*)renderPass;
			inheritanceInfo.framebuffer = *(VulkanFrameBuffer*)framebuffer;
			VkCommandBufferBeginInfo beginCI{};
			beginCI.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
			beginCI.flags = VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT;
			beginCI.pInheritanceInfo = &inheritanceInfo;

			VK_CHECK_RESULT(vkBeginCommandBuffer(commandBuffer, &beginCI));
			*/
		}
		else
		{
			VkCommandBufferBeginInfo beginCI{};
			beginCI.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
			beginCI.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
			VK_CHECK_RESULT(vkBeginCommandBuffer(commandBuffer, &beginCI));
		}
		state = AshCommandBufferState::ASH_Recording;
	}
	auto VulkanCommandBuffer::end() -> void
	{
		H_ASSERTLOG(state == AshCommandBufferState::ASH_Recording, "CommandBuffer ended before started recording");
		VK_CHECK_RESULT(vkEndCommandBuffer(commandBuffer));
		state = AshCommandBufferState::ASH_Ended;
	}
	auto VulkanCommandBuffer::get_state() -> AshCommandBufferState
	{
		return state;
	}
}