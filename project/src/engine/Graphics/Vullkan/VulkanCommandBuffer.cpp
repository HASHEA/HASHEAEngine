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
			cmdBuffer.init(poolIndex,false);
		}
		uint32_t index = totalBuffers;
		for (uint32_t poolIndex = 0; poolIndex < totalPools; poolIndex++)
		{
			for (uint32_t scbIndex = 0; scbIndex < k_secondary_command_buffers_count; scbIndex++)
			{
				VulkanCommandBuffer& scb = secondaryCommandBuffers[index - totalBuffers];
				scb.init(poolIndex,true);
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
	//it's a out only get, u don't need to recycle it 
	auto VulkanCommandBufferManager::get_command_buffer(uint32_t frameIndex, uint32_t threadIndex) -> VulkanCommandBuffer*
	{
		auto poolIndex = pool_from_indices(frameIndex,threadIndex);
		auto& curCount = usedBuffers[poolIndex];
		H_ASSERTLOG(curCount <= numCommandBuffersPerThread, "used commandbuffer count exceed max count per thread {0} > {1}", curCount,numCommandBuffersPerThread);
		VulkanCommandBuffer* ret = &commandBuffers[frameIndex * numCommandBuffersPerThread + curCount];
		curCount++;
		return ret;
	}
	auto VulkanCommandBufferManager::get_secondary_command_buffer(uint32_t frameIndex, uint32_t threadIndex) -> VulkanCommandBuffer*
	{
		return {};
	}
	uint32_t VulkanCommandBufferManager::pool_from_indices(uint32_t frame_index, uint32_t thread_index)
	{
		return frame_index * numPoolsPerFrame + thread_index;
	}
	auto VulkanCommandBuffer::init( uint32_t poolIndex, bool _secondary) -> void
	{
		this->secondary = _secondary;
		vkCommandPool = VulkanContext::get_frame_pool(poolIndex).cmdPool->get_handle();
		VkCommandBufferAllocateInfo cmd = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
		cmd.pNext = nullptr;
		cmd.commandPool = vkCommandPool;
		cmd.level = _secondary ? VK_COMMAND_BUFFER_LEVEL_SECONDARY : VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		cmd.commandBufferCount = 1;
		VK_CHECK_RESULT(vkAllocateCommandBuffers(VulkanContext::get_vulkan_device(), &cmd, &vkCommandBuffer));

		static const uint32_t k_global_pool_elements = 128;
		VkDescriptorPoolSize pool_sizes[] =
		{
			{ VK_DESCRIPTOR_TYPE_SAMPLER, k_global_pool_elements },
			{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, k_global_pool_elements },
			{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, k_global_pool_elements },
			{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, k_global_pool_elements },
			{ VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, k_global_pool_elements },
			{ VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, k_global_pool_elements },
			{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, k_global_pool_elements },
			{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, k_global_pool_elements },
			{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, k_global_pool_elements },
			{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, k_global_pool_elements },
			{ VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, k_global_pool_elements}
		};
		VkDescriptorPoolCreateInfo pool_info = {};
		pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
		pool_info.maxSets = k_descriptor_sets_pool_size;
		pool_info.poolSizeCount = (uint32_t)ArraySize(pool_sizes);
		pool_info.pPoolSizes = pool_sizes;
		VK_CHECK_RESULT(vkCreateDescriptorPool(VulkanContext::get_vulkan_device(), &pool_info, VulkanContext::get_vulkan_allocation_callbacks(), &vk_descriptor_pool));
	}
	auto VulkanCommandBuffer::shutdown() -> void
	{
		if (vkCommandBuffer)
			vkFreeCommandBuffers(VulkanContext::get_vulkan_device(), vkCommandPool,1, &vkCommandBuffer);
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
			VK_CHECK_RESULT(vkBeginCommandBuffer(vkCommandBuffer, &beginCI));
		}
		state = AshCommandBufferState::ASH_Recording;
	}
	auto VulkanCommandBuffer::end() -> void
	{
		H_ASSERTLOG(state == AshCommandBufferState::ASH_Recording, "CommandBuffer ended before started recording");
		VK_CHECK_RESULT(vkEndCommandBuffer(vkCommandBuffer));
		state = AshCommandBufferState::ASH_Ended;
	}
	auto VulkanCommandBuffer::get_state() -> AshCommandBufferState
	{
		return state;
	}
	auto VulkanCommandBuffer::transition_image_state(std::shared_ptr<Texture> texture, AshResourceState newlayout, TextureSubResource* region
		, AshQueueType::Enum srcQueueType, AshQueueType::Enum dstQueueType ) -> void
	{
		H_ASSERTLOG(vkCommandBuffer != VK_NULL_HANDLE,"Fatal: try to access a invalid vkCommandBuffer !");
		H_ASSERTLOG(state == ASH_Recording," you need call begin() before write any command ! ");
		H_ASSERTLOG((srcQueueType != dstQueueType) && (dstQueueType == AshQueueType::Enum::Ignored), " invalid dst queue type ! ");
		if (srcQueueType == dstQueueType)
		{
			if (!ash_is_valid_transition(texture->get_resource_state(), newlayout))
			{
				return;
			}
		}
		auto vkFormat = ash_format_to_vk(texture->get_format());
		bool depth = TextureFormat::has_depth(vkFormat);
		bool stencil = TextureFormat::has_stencil(vkFormat);
		VkImageSubresourceRange subresourceRange = {};
		subresourceRange.aspectMask = depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
		if (stencil)
		{
			subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
		}
		if (!region) //full texture all layer
		{		
			subresourceRange.baseMipLevel = 0;
			subresourceRange.levelCount = texture->get_mip_maps_count();
			subresourceRange.layerCount = texture->get_layer_count();
			subresourceRange.baseArrayLayer = 0;
		}
		else
		{
			H_ASSERT(region->mip_level_count <= texture->get_mip_maps_count());
			H_ASSERT(region->array_layer_count <= texture->get_layer_count());
			subresourceRange.baseMipLevel = region->mip_base_level;
			subresourceRange.levelCount = region->mip_level_count;
			subresourceRange.layerCount = region->array_layer_count;
			subresourceRange.baseArrayLayer = region->array_base_layer;
		}

		auto srcLayout = ash_resource_state_to_vk_image_layout(texture->get_resource_state());
		auto dstLayout = ash_resource_state_to_vk_image_layout(newlayout);
		VkImageMemoryBarrier imageMemoryBarrier{};
		imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		imageMemoryBarrier.srcQueueFamilyIndex = VulkanContext::get_queue_family_index(srcQueueType);//TODO: add belonging transition
		imageMemoryBarrier.dstQueueFamilyIndex = VulkanContext::get_queue_family_index(dstQueueType);
		imageMemoryBarrier.oldLayout = srcLayout;
		imageMemoryBarrier.newLayout = dstLayout;
		imageMemoryBarrier.image = (VkImage)(texture->get_native_texture_handle());
		imageMemoryBarrier.subresourceRange = subresourceRange;
		imageMemoryBarrier.srcAccessMask = vk_layout_to_access_mask(srcLayout);
		imageMemoryBarrier.dstAccessMask = vk_layout_to_access_mask(dstLayout);


		const VkPipelineStageFlags source_stage_mask = util_determine_pipeline_stage_flags(imageMemoryBarrier.srcAccessMask, srcQueueType);
		const VkPipelineStageFlags destination_stage_mask = util_determine_pipeline_stage_flags(imageMemoryBarrier.dstAccessMask, dstQueueType);

		vkCmdPipelineBarrier(vkCommandBuffer, source_stage_mask, destination_stage_mask, 0,
			0, nullptr, 0, nullptr, 1, &imageMemoryBarrier);
		// may not be true because the async between gpu and cpu
		// anyway, how to track resource state?
		texture->set_resource_state(newlayout);
	}
	auto VulkanCommandBuffer::get_native_handle() -> void*
	{
		return vkCommandBuffer;
	}
	auto VulkanCommandBuffer::set_state(AshCommandBufferState _state) -> void
	{
		state = _state;
	}
}