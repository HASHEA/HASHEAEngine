#include "VulkanCommandPool.h"
#include "VulkanCommandBuffer.h"
#include "VulkanContext.h"
#include "VulkanFramebuffer.h"
#include "VulkanRenderPass.h"
namespace RHI
{
	auto VulkanCommandBufferManager::init(uint32_t numThread) -> void
	{
		numPoolsPerFrame = numThread;
		const uint32_t totalPools = numPoolsPerFrame * k_max_frames;
		usedBuffers.init(nullptr, totalPools, totalPools);
		usedSecondaryCommandBuffers.init(nullptr, totalPools, totalPools);
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
	auto VulkanCommandBufferManager::reset(uint32_t frameIndex) -> void
	{
		for (uint32_t i = 0; i < numPoolsPerFrame; i++) {
			const uint32_t pool_index = pool_from_indices(frameIndex, i);
			usedBuffers[pool_index] = 0;
			usedSecondaryCommandBuffers[pool_index] = 0;
		}
	}
	//it's a out only get, u don't need to recycle it 
	auto VulkanCommandBufferManager::get_command_buffer(uint32_t frameIndex, uint32_t threadIndex) -> VulkanCommandBuffer*
	{
		auto poolIndex = pool_from_indices(frameIndex,threadIndex);
		auto curCount = usedBuffers[poolIndex];
		H_ASSERTLOG(curCount <= numCommandBuffersPerThread, "used commandbuffer count exceed max count per thread {0} > {1}", curCount,numCommandBuffersPerThread);
		VulkanCommandBuffer* ret = &commandBuffers[frameIndex * numCommandBuffersPerThread + curCount];
		usedBuffers[poolIndex] = curCount + 1;
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
		if (vkCommandBuffer != VK_NULL_HANDLE)
		{
			vkFreeCommandBuffers(VulkanContext::get_vulkan_device(), vkCommandPool, 1, &vkCommandBuffer);
		}
		if (vk_descriptor_pool != VK_NULL_HANDLE)
		{
			vkDestroyDescriptorPool(VulkanContext::get_vulkan_device(), vk_descriptor_pool, VulkanContext::get_vulkan_allocation_callbacks());
			vk_descriptor_pool = VK_NULL_HANDLE;
		}
	}

	auto VulkanCommandBuffer::begin() -> void
	{
		H_ASSERTLOG(vkCommandBuffer != VK_NULL_HANDLE, "Fatal: try to access a invalid vkCommandBuffer !");
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
	//TODO:: merge barriers
	auto VulkanCommandBuffer::transition_image_state(std::shared_ptr<Texture> texture, AshResourceState newlayout, TextureSubResource* region
		, AshQueueType::Enum srcQueueType, AshQueueType::Enum dstQueueType ) -> void
	{
		H_ASSERTLOG(state == ASH_Recording, " you need call begin() before recording any command ! ");
		H_ASSERTLOG((srcQueueType == dstQueueType) || (dstQueueType != AshQueueType::Enum::Ignored), " invalid dst queue type ! ");
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
		imageMemoryBarrier.image = (VkImage)(texture->get_native_handle());
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
	auto VulkanCommandBuffer::begin_render_pass(std::shared_ptr<Framebuffer> frameBuffer) -> void
	{
		H_ASSERTLOG(state == ASH_Recording, " you need call begin() before recording any command ! ");
		auto renderPass = frameBuffer->get_render_pass();
		if (currentBoundRenderPass != nullptr)
		{
			HLogWarning("the last render pass hasn't been ended ! It's ok if you want to automaticly end it and bind new one !");
			end_render_pass();
		}
		if (renderPass == currentBoundRenderPass)
		{
			HLogWarning("Bind a render pass which is bound currently on this commandbuffer, do nothing and return !");
			return;
		}
		//insure all attachment are in correct layout
		auto colorAttachements = frameBuffer->get_render_targets();
		auto depthStencilAttachment = frameBuffer->get_depth_stencil();
		auto shadingRateAttachment = frameBuffer->get_shading_rate_attachment();
		auto renderTargetCount = frameBuffer->get_render_targets().size();
		if (VulkanContext::get()->get_device_extension_enabled(DeviceExtensionAndFeaturesFlags::DynamicRendering))
		{
			//only dynamic render need to transition images
			for (auto i = 0; i < renderTargetCount; i++)
			{
				transition_image_state(colorAttachements[i], ASH_RESOURCE_STATE_RENDER_TARGET);
			}
			if (depthStencilAttachment != nullptr)
			{
				transition_image_state(depthStencilAttachment, ASH_RESOURCE_STATE_DEPTH_STENCIL_WRITE);
			}
			if (shadingRateAttachment != nullptr)
			{
				transition_image_state(shadingRateAttachment, ASH_RESOURCE_STATE_FRAGMENT_SHADING_RATE_ATTACHMENT);
			}
			VkRenderingAttachmentInfoKHR color_attachments_info[k_max_image_outputs] = {};
			for (auto i = 0; i < renderTargetCount; i++)
			{
				VkAttachmentLoadOp color_op{};
				color_op = ash_load_operation_to_vk(renderPass->get_color_operations()[i]);
				VkRenderingAttachmentInfoKHR& color_attachment_info = color_attachments_info[i];
				color_attachment_info.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR;
				color_attachment_info.imageView = (VkImageView)colorAttachements[i]->get_default_render_target_view()->get_native_handle();
				color_attachment_info.imageLayout = VulkanContext::get()->get_device_extension_enabled(DeviceExtensionAndFeaturesFlags::Synchronization2) ? VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL_KHR : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
				color_attachment_info.resolveMode = VK_RESOLVE_MODE_NONE;
				color_attachment_info.loadOp = color_op;
				color_attachment_info.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
				VkClearValue clearValue{ };
				clearValue.color = ash_color_value_to_vk(frameBuffer->get_render_target_clear_color(i));
				color_attachment_info.clearValue = renderPass->get_color_operations()[i] == AshLoadOption::ASH_LOAD_CLEAR ? clearValue : VkClearValue{ };
			}
			VkRenderingAttachmentInfoKHR depth_attachment_info{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR };
			bool has_depth_attachment = depthStencilAttachment != nullptr;
			if (has_depth_attachment) {
				VkAttachmentLoadOp depth_op;
				depth_op = ash_load_operation_to_vk(renderPass->get_depth_stencil_operations());
				depth_attachment_info.imageView = (VkImageView)depthStencilAttachment->get_default_render_target_view()->get_native_handle();
				depth_attachment_info.imageLayout = VulkanContext::get()->get_device_extension_enabled(DeviceExtensionAndFeaturesFlags::Synchronization2) ? VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL_KHR : VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
				depth_attachment_info.resolveMode = VK_RESOLVE_MODE_NONE;
				depth_attachment_info.loadOp = depth_op;
				depth_attachment_info.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
				VkClearValue clearValue{ };
				clearValue.depthStencil = ash_depth_stencil_value_to_vk(frameBuffer->get_depth_stencil_clear_color());
				depth_attachment_info.clearValue = renderPass->get_depth_stencil_operations() == AshLoadOption::ASH_LOAD_CLEAR ? clearValue : VkClearValue{ };
			}
			VkRenderingInfoKHR rendering_info{ VK_STRUCTURE_TYPE_RENDERING_INFO_KHR };
			rendering_info.flags = secondary ? VK_RENDERING_CONTENTS_SECONDARY_COMMAND_BUFFERS_BIT_KHR : 0;
			rendering_info.renderArea = { 0, 0, frameBuffer->get_width(), frameBuffer->get_height() };
			rendering_info.layerCount = frameBuffer->get_layer_count();
			rendering_info.viewMask = renderPass->get_multiview_mask();
			rendering_info.colorAttachmentCount = renderTargetCount;
			rendering_info.pColorAttachments = renderTargetCount > 0 ? color_attachments_info : nullptr;
			rendering_info.pDepthAttachment = has_depth_attachment ? &depth_attachment_info : nullptr;
			rendering_info.pStencilAttachment = nullptr;
			VkRenderingFragmentShadingRateAttachmentInfoKHR shading_rate_info{ VK_STRUCTURE_TYPE_RENDERING_FRAGMENT_SHADING_RATE_ATTACHMENT_INFO_KHR };
			auto srattachment = frameBuffer->get_shading_rate_attachment();
			if (srattachment != nullptr) {
				shading_rate_info.imageView = (VkImageView)srattachment->get_default_render_target_view()->get_native_handle();
				shading_rate_info.imageLayout = VK_IMAGE_LAYOUT_FRAGMENT_SHADING_RATE_ATTACHMENT_OPTIMAL_KHR;
				shading_rate_info.shadingRateAttachmentTexelSize = VulkanContext::get_fragment_shading_rate_texel_size();
				rendering_info.pNext = (void*)&shading_rate_info;
			}
			vkCmdBeginRenderingKHR(vkCommandBuffer, &rendering_info);
		}
		else
		{
			VkRenderPassBeginInfo render_pass_begin{ VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
			render_pass_begin.framebuffer = (VkFramebuffer)frameBuffer->get_native_handle();
			render_pass_begin.renderPass = (VkRenderPass)renderPass->get_native_handle();
			render_pass_begin.renderArea.offset = { 0, 0 };
			render_pass_begin.renderArea.extent = { frameBuffer->get_width(), frameBuffer->get_height() };
			VkClearValue clearValues[k_max_image_outputs + 1];
			uint32_t clearValueCount = renderTargetCount;
			for (uint32_t i = 0; i < renderTargetCount; i++)
			{
				clearValues[i].color = ash_color_value_to_vk(frameBuffer->get_render_target_clear_color(i));
			}
			if (depthStencilAttachment != nullptr)
			{
				clearValues[renderTargetCount].depthStencil = ash_depth_stencil_value_to_vk(frameBuffer->get_depth_stencil_clear_color());
				clearValueCount++;
			}
			render_pass_begin.clearValueCount = clearValueCount;
			render_pass_begin.pClearValues = clearValues;
			vkCmdBeginRenderPass(vkCommandBuffer, &render_pass_begin, secondary ? VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS : VK_SUBPASS_CONTENTS_INLINE);
		}
		currentBoundRenderPass = renderPass;
		currentBoundFramebuffer = frameBuffer;
	}
	auto VulkanCommandBuffer::end_render_pass() -> void
	{
		H_ASSERTLOG(state == ASH_Recording, " you need call begin() before recording any command ! ");
		if (currentBoundRenderPass == nullptr)
		{
			HLogWarning("end_render_pass : none renderpass are bound currently, do nothing and return !");
			return;
		}
		if (VulkanContext::get()->get_device_extension_enabled(DeviceExtensionAndFeaturesFlags::DynamicRendering)) {
			vkCmdEndRenderingKHR(vkCommandBuffer);
		}
		else {
			vkCmdEndRenderPass(vkCommandBuffer);
			//manually set attachment state changed by renderpass for tracking state
			auto colorAttachments = currentBoundFramebuffer->get_render_targets();
			auto count = colorAttachments.size();
			auto depthAttachment = currentBoundFramebuffer->get_depth_stencil();
			for (auto i = 0; i < count; i++)
			{
				colorAttachments[i]->set_resource_state(currentBoundRenderPass->get_color_attachment_final_state(i));
			}
			if (depthAttachment != nullptr)
			{
				depthAttachment->set_resource_state(currentBoundRenderPass->get_depth_stencil_attachment_final_state());
			}
		}
		currentBoundRenderPass = nullptr;
		currentBoundFramebuffer = nullptr;
	}
	auto VulkanCommandBuffer::bind_pipeline() -> void
	{
		H_ASSERTLOG(state == ASH_Recording, " you need call begin() before recording any command ! ");

	}
	auto VulkanCommandBuffer::set_state(AshCommandBufferState _state) -> void
	{
		state = _state;
	}
}