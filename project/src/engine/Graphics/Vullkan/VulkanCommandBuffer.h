#pragma once
#include "VulkanHelper.hpp"
#include "VulkanWrapper.h"
#include "Graphics/CommandBuffer.h"
#include "Base/ds/harray.hpp"
using namespace AshEngine;
namespace RHI
{
	static const uint32_t k_secondary_command_buffers_count = 2;
	struct FramePool;
	class VulkanCommandBuffer : public CommandBuffer
	{
	public:
		VulkanCommandBuffer() = default;
		~VulkanCommandBuffer() {};
		NO_COPYABLE(VulkanCommandBuffer);
		auto init(uint32_t index,const FramePool* framePool) -> void;
		auto shutdown() -> void;
		inline auto const get_vkCommandBuffer() const
		{
			return commandBuffer;
		}
		inline auto get_index() const
		{
			return index;
		}
		auto allocate_vulkan_command_buffer(const VkCommandBufferAllocateInfo* const cmd) -> void;
	private:
		VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
		FramePool* framePool = nullptr;
		uint32_t index = 0;
	};
	struct VulkanCommandBufferManager
	{
		auto init(uint32_t numThread) -> void;
		auto shutdown() -> void;
		auto reset_pools(uint32_t frameIndex) -> void;
		auto get_command_buffer(uint32_t frameIndex, uint32_t threadIndex, bool begin) -> VulkanCommandBuffer*;
		auto get_secondary_command_buffer(uint32_t frameIndex, uint32_t threadIndex) -> VulkanCommandBuffer*;
		Array<VulkanCommandBuffer>    commandBuffers;
		Array<VulkanCommandBuffer>    secondaryCommandBuffers;
		Array<uint8_t>				  usedBuffers;       // Track how many buffers were used per thread per frame.
		Array<uint8_t>				  usedSecondaryCommandBuffers;
		uint32_t                      numPoolsPerFrame = 0;
		uint32_t                      numCommandBuffersPerThread = 3;
		uint32_t					  pool_from_indices(uint32_t frame_index, uint32_t thread_index);
	};	
};