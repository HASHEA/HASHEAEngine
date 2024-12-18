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
	struct VulkanCommandBufferManager;
	class VulkanCommandBuffer : public CommandBuffer
	{
	public:
		VulkanCommandBuffer() = default;
		~VulkanCommandBuffer() {};
	private:	
		auto init(uint32_t allocIndex, uint32_t poolIndex, bool secondary) -> void;
		auto shutdown() -> void;
	public:
		inline auto const get_vkCommandBuffer() const
		{
			return commandBuffer;
		}
		inline auto get_index() const
		{
			return index;
		}
		inline auto is_secondary() const
		{
			return secondary;
		}
	public:
		//rhi interfaces
		virtual auto begin()  -> void override;
		virtual auto end() -> void override;
		virtual auto get_state() ->AshCommandBufferState override;
	private:
		VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
		VkCommandPool   commandPool = VK_NULL_HANDLE;
		uint32_t index = 0;
		bool secondary = false;
		AshCommandBufferState state = AshCommandBufferState::ASH_Idle;
		

		friend class VulkanCommandBufferManager;

	};
	struct VulkanCommandBufferManager
	{
		auto init(uint32_t numThread) -> void;
		auto shutdown() -> void;
		auto reset_pools(uint32_t frameIndex) -> void;
		auto get_command_buffer(uint32_t frameIndex, uint32_t threadIndex) -> VulkanCommandBuffer;
		auto get_secondary_command_buffer(uint32_t frameIndex, uint32_t threadIndex) -> VulkanCommandBuffer;
		Array<VulkanCommandBuffer>    commandBuffers;
		Array<VulkanCommandBuffer>    secondaryCommandBuffers;
		Array<uint8_t>				  usedBuffers;       // Track how many buffers were used per thread per frame.
		Array<uint8_t>				  usedSecondaryCommandBuffers;
		uint32_t                      numPoolsPerFrame = 0;
		uint32_t                      numCommandBuffersPerThread = 3;
		uint32_t					  pool_from_indices(uint32_t frame_index, uint32_t thread_index);
	};	
};