#include "VulkanCommandPool.h"
#include "VulkanCommandBuffer.h"
#include "VulkanContext.h"
#include "VulkanFramebuffer.h"
#include "VulkanRenderPass.h"
#include "VulkanBuffer.h"
#include "VulkanTexture.h"
#include "VulkanStagingBuffer.h"
#include "Graphics/CommandBuffer.h"
#include "Graphics/Buffer.h"
#include "Graphics/TextureUploadUtils.h"
#include <cstring>
#include <numeric>
namespace RHI
{
	namespace
	{
		static bool is_vk_access_read_only(VkAccessFlags access_flags)
		{
			static const VkAccessFlags k_read_mask =
				VK_ACCESS_INDIRECT_COMMAND_READ_BIT |
				VK_ACCESS_INDEX_READ_BIT |
				VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT |
				VK_ACCESS_UNIFORM_READ_BIT |
				VK_ACCESS_INPUT_ATTACHMENT_READ_BIT |
				VK_ACCESS_SHADER_READ_BIT |
				VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
				VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
				VK_ACCESS_TRANSFER_READ_BIT;
			return (access_flags & (~k_read_mask)) == 0;
		}
	}

	static void get_vk_stage_and_access_flags(AshResourceState RHIAccess, AshBarrier::EType ResourceType, uint32_t UsageFlags,
		bool bIsDepthStencil, VkPipelineStageFlags& StageFlags, VkAccessFlags& AccessFlags, VkImageLayout& Layout, bool bIsSourceState)
	{
		const bool use_sync2_attachment_layouts =
			VulkanContext::get()->get_device_extension_enabled(DeviceExtensionAndFeaturesFlags::Synchronization2);
		// From Vulkan's point of view, when performing a multisample resolve via a render pass attachment, resolve targets are the same as render targets .
		// The caller signals this situation by setting both the RTV and ResolveDst flags, and we simply remove ResolveDst in that case,
		// to treat the resource as a render target.
		const AshResourceState ResolveAttachmentAccess = (AshResourceState)(AshResourceState::RTV | AshResourceState::ResolveDst);
		if (RHIAccess == ResolveAttachmentAccess)
		{
			RHIAccess = AshResourceState::RTV;
		}

		Layout = VK_IMAGE_LAYOUT_UNDEFINED;

		// The layout to use if SRV access is requested. In case of depth/stencil buffers, we don't need to worry about different states for the separate aspects, since that's handled explicitly elsewhere,
		// and this function is never called for depth-only or stencil-only transitions.
		const VkImageLayout SRVLayout = bIsDepthStencil ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		// States which cannot be combined.
		switch (RHIAccess)
		{
		case AshResourceState::Unknown:
			// We don't know where this is coming from, so we'll stall everything.
			StageFlags = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
			AccessFlags = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
			return;

		case AshResourceState::CPURead:
			// FIXME: is this correct?
			StageFlags = VK_PIPELINE_STAGE_HOST_BIT;
			AccessFlags = VK_ACCESS_HOST_READ_BIT;
			Layout = VK_IMAGE_LAYOUT_GENERAL;
			return;

		case AshResourceState::Present:
			// Swapchain acquire waits are submitted with COLOR_ATTACHMENT_OUTPUT. Using TOP_OF_PIPE
			// here leaves the present->render transition outside the waited stage scope and triggers
			// WRITE_AFTER_READ hazards on the acquired image.
			StageFlags = bIsSourceState ? VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT : VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
			AccessFlags = 0;
			Layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
			return;

		case AshResourceState::RTV:
			StageFlags = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
			AccessFlags = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
			Layout = use_sync2_attachment_layouts ? VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL_KHR : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			return;

		case AshResourceState::CopyDst:
			StageFlags = VK_PIPELINE_STAGE_TRANSFER_BIT;
			AccessFlags = VK_ACCESS_TRANSFER_WRITE_BIT;
			Layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			return;

		case AshResourceState::ResolveDst:
			// Used when doing a resolve via RHICopyToResolveTarget. For us, it's the same as CopyDst.
			StageFlags = VK_PIPELINE_STAGE_TRANSFER_BIT;
			AccessFlags = VK_ACCESS_TRANSFER_WRITE_BIT;
			Layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			return;

		case AshResourceState::SBTRead:
			StageFlags = VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR;
			AccessFlags = VK_ACCESS_MEMORY_READ_BIT;
			return;
		}

		// If DSVWrite is set, we ignore everything else because it decides the layout.
		if (has_any_flags((uint32_t)RHIAccess, (uint32_t)AshResourceState::DSVWrite))
		{
			H_ASSERT(bIsDepthStencil);
			StageFlags = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
			AccessFlags = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
			Layout = use_sync2_attachment_layouts ? VK_IMAGE_LAYOUT_ATTACHMENT_OPTIMAL_KHR : VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
			return;
		}

		// The remaining flags can be combined.
		StageFlags = 0;
		AccessFlags = 0;

		if (has_any_flags((uint32_t)RHIAccess, (uint32_t)AshResourceState::IndirectArgs))
		{
			H_ASSERT(ResourceType != AshBarrier::EType::Texture);
			StageFlags |= VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT;
			AccessFlags |= VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
		}

		if (has_any_flags((uint32_t)RHIAccess, (uint32_t)AshResourceState::ConstBuffer))
		{
			H_ASSERT(ResourceType != AshBarrier::EType::Texture);
			StageFlags |= VK_PIPELINE_STAGE_VERTEX_SHADER_BIT;
			AccessFlags |= VK_ACCESS_UNIFORM_READ_BIT;
		}

		if (has_any_flags((uint32_t)RHIAccess, (uint32_t)AshResourceState::VertexBuffer) || has_any_flags((uint32_t)RHIAccess, (uint32_t)AshResourceState::IndexBuffer))
		{
			H_ASSERT(ResourceType != AshBarrier::EType::Texture);
			StageFlags |= VK_PIPELINE_STAGE_VERTEX_INPUT_BIT;
			switch (ResourceType)
			{
			case AshBarrier::EType::Buffer:
				if ((UsageFlags & VK_BUFFER_USAGE_INDEX_BUFFER_BIT) != 0)
				{
					AccessFlags |= VK_ACCESS_INDEX_READ_BIT;
				}
				if ((UsageFlags & VK_BUFFER_USAGE_VERTEX_BUFFER_BIT) != 0)
				{
					AccessFlags |= VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
				}
				break;
			default:
				H_ASSERT(false);
				break;
			}
		}

		if (has_any_flags((uint32_t)RHIAccess, (uint32_t)AshResourceState::DSVRead))
		{
			H_ASSERT(bIsDepthStencil);
			StageFlags |= VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
			AccessFlags |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;

			// If any of the SRV flags is set, the code below will set Layout to SRVLayout again, but it's fine since
			// SRVLayout takes into account bIsDepthStencil and ends up being the same as what we set here.
			Layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
		}

		if (has_any_flags((uint32_t)RHIAccess, (uint32_t)AshResourceState::SRVGraphics))
		{
			StageFlags |= VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_TESSELLATION_CONTROL_SHADER_BIT | VK_PIPELINE_STAGE_TESSELLATION_EVALUATION_SHADER_BIT | VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
			AccessFlags |= VK_ACCESS_SHADER_READ_BIT;

			Layout = SRVLayout;
		}

		if (has_any_flags((uint32_t)RHIAccess, (uint32_t)AshResourceState::SRVCompute))
		{
			StageFlags |= VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
			AccessFlags |= VK_ACCESS_SHADER_READ_BIT;
			// There are cases where we ping-pong images between UAVCompute and SRVCompute. In that case it may be more efficient to leave the image in VK_IMAGE_LAYOUT_GENERAL
			// (at the very least, it will mean fewer image barriers). There's no good way to detect this though, so it might be better if the high level code just did UAV
			// to UAV transitions in that case, instead of SRV <-> UAV.
			Layout = SRVLayout;
		}

		if (has_any_flags((uint32_t)RHIAccess, (uint32_t)AshResourceState::UAVGraphics))
		{
			StageFlags |= VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_TESSELLATION_CONTROL_SHADER_BIT | VK_PIPELINE_STAGE_TESSELLATION_EVALUATION_SHADER_BIT | VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
			AccessFlags |= VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
			Layout = VK_IMAGE_LAYOUT_GENERAL;
		}

		if (has_any_flags((uint32_t)RHIAccess, (uint32_t)AshResourceState::UAVCompute))
		{
			StageFlags |= VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
			AccessFlags |= VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
			Layout = VK_IMAGE_LAYOUT_GENERAL;
		}

		if (has_any_flags((uint32_t)RHIAccess, (uint32_t)(AshResourceState::CopySrc | AshResourceState::ResolveSrc)))
		{
			// ResolveSrc is used when doing a resolve via RHICopyToResolveTarget. For us, it's the same as CopySrc.
			StageFlags = VK_PIPELINE_STAGE_TRANSFER_BIT;
			AccessFlags = VK_ACCESS_TRANSFER_READ_BIT;
			if (ResourceType == AshBarrier::EType::Texture)
			{
				// If this is requested for a texture, make sure it's not combined with other access flags which require a different layout. It's important
				// that this block is last, so that if any other flags set the layout before, we trigger the assert below.
				H_ASSERT(Layout == VK_IMAGE_LAYOUT_UNDEFINED);
				Layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			}
		}
	}

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
			for (uint32_t bufferIndex = 0; bufferIndex < numCommandBuffersPerThread; ++bufferIndex)
			{
				commandBuffers[pool_index * numCommandBuffersPerThread + bufferIndex].set_state(AshCommandBufferState::ASH_Idle);
			}
			for (uint32_t bufferIndex = 0; bufferIndex < k_secondary_command_buffers_count; ++bufferIndex)
			{
				secondaryCommandBuffers[pool_index * k_secondary_command_buffers_count + bufferIndex].set_state(AshCommandBufferState::ASH_Idle);
			}
		}
	}
	//it's a out only get, u don't need to recycle it 
	auto VulkanCommandBufferManager::get_command_buffer(uint32_t frameIndex, uint32_t threadIndex) -> VulkanCommandBuffer*
	{
		auto poolIndex = pool_from_indices(frameIndex,threadIndex);
		auto curCount = usedBuffers[poolIndex];
		H_ASSERTLOG(curCount < numCommandBuffersPerThread, "used commandbuffer count exceed max count per thread {0} >= {1}", curCount, numCommandBuffersPerThread);
		VulkanCommandBuffer* ret = &commandBuffers[poolIndex * numCommandBuffersPerThread + curCount];
		usedBuffers[poolIndex] = curCount + 1;
		return ret;
	}
	auto VulkanCommandBufferManager::get_secondary_command_buffer(uint32_t frameIndex, uint32_t threadIndex) -> VulkanCommandBuffer*
	{
		auto poolIndex = pool_from_indices(frameIndex, threadIndex);
		auto curCount = usedSecondaryCommandBuffers[poolIndex];
		H_ASSERTLOG(curCount < k_secondary_command_buffers_count, "used secondary commandbuffer count exceed max count per thread {0} >= {1}", curCount, k_secondary_command_buffers_count);
		VulkanCommandBuffer* ret = &secondaryCommandBuffers[poolIndex * k_secondary_command_buffers_count + curCount];
		usedSecondaryCommandBuffers[poolIndex] = curCount + 1;
		return ret;
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
			vkCommandBuffer = VK_NULL_HANDLE;
		}
		if (vk_descriptor_pool != VK_NULL_HANDLE)
		{
			vkDestroyDescriptorPool(VulkanContext::get_vulkan_device(), vk_descriptor_pool, VulkanContext::get_vulkan_allocation_callbacks());
			vk_descriptor_pool = VK_NULL_HANDLE;
		}
	}

	auto VulkanCommandBuffer::begin_record() -> void
	{
		H_ASSERTLOG(vkCommandBuffer != VK_NULL_HANDLE, "Fatal: try to access a invalid vkCommandBuffer !");
		VK_CHECK_RESULT(vkResetDescriptorPool(VulkanContext::get_vulkan_device(), vk_descriptor_pool, 0));
		currentBoundRenderPass = nullptr;
		currentBoundFramebuffer = nullptr;
		if (secondary)
		{
			VkCommandBufferBeginInfo beginCI{};
			beginCI.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
			beginCI.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
			beginCI.pInheritanceInfo = nullptr;
			VK_CHECK_RESULT(vkBeginCommandBuffer(vkCommandBuffer, &beginCI));
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
	auto VulkanCommandBuffer::end_record() -> void
	{
		H_ASSERTLOG(state == AshCommandBufferState::ASH_Recording, "CommandBuffer ended before started recording");
		VK_CHECK_RESULT(vkEndCommandBuffer(vkCommandBuffer));
		state = AshCommandBufferState::ASH_Ended;
	}
	auto VulkanCommandBuffer::get_state() -> AshCommandBufferState
	{
		return state;
	}
	
	auto VulkanCommandBuffer::get_native_handle() -> void*
	{
		return vkCommandBuffer;
	}
	auto VulkanCommandBuffer::cmd_begin_render_pass(std::shared_ptr<Framebuffer> frameBuffer) -> void
	{
		H_ASSERTLOG(state == ASH_Recording, " you need call begin() before recording any command ! ");
		auto renderPass = frameBuffer->get_render_pass();
		if (currentBoundRenderPass != nullptr)
		{
			HLogWarning("the last render pass hasn't been ended ! It's ok if you want to automaticly end it and bind new one !");
			cmd_end_render_pass();
		}
		if (renderPass == currentBoundRenderPass)
		{
			HLogWarning("Bind a render pass which is bound currently on this commandbuffer, do nothing and return !");
			return;
		}
		//insure all attachment are in correct layout
		auto& colorAttachements = frameBuffer->get_render_targets();
		auto depthStencilAttachment = frameBuffer->get_depth_stencil();
		auto shadingRateAttachment = frameBuffer->get_shading_rate_attachment();
		auto renderTargetCount = frameBuffer->get_render_targets().size();
		if (VulkanContext::get()->get_device_extension_enabled(DeviceExtensionAndFeaturesFlags::DynamicRendering))
		{
			//only dynamic render need to transition images
			bool require_attachment_load_sync = false;
			for (auto i = 0; i < renderTargetCount; i++)
			{
				cmd_transition_resource_state({colorAttachements[i], AshResourceState::RTV });
				require_attachment_load_sync = require_attachment_load_sync || renderPass->get_color_operations()[i] == AshLoadOption::ASH_LOAD_LOAD;
			}
			if (depthStencilAttachment != nullptr)
			{
				cmd_transition_resource_state({ depthStencilAttachment, AshResourceState::DSVWrite });
				require_attachment_load_sync = require_attachment_load_sync || renderPass->get_depth_stencil_operations() == AshLoadOption::ASH_LOAD_LOAD;
			}
			if (shadingRateAttachment != nullptr)
			{
				cmd_transition_resource_state({ shadingRateAttachment, AshResourceState::ShadingRateSource });
			}
			if (require_attachment_load_sync)
			{
				// Attachment loadOp reads happen at COLOR_ATTACHMENT_OUTPUT / EARLY/LATE_FRAGMENT_TESTS.
				// When we transition an attachment into renderable layout immediately before begin rendering,
				// add an explicit execution+memory dependency so the load is ordered after that transition.
				VkMemoryBarrier attachment_load_barrier{ VK_STRUCTURE_TYPE_MEMORY_BARRIER };
				attachment_load_barrier.srcAccessMask =
					VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
					VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
				attachment_load_barrier.dstAccessMask =
					VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
					VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
					VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
					VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
				vkCmdPipelineBarrier(
					vkCommandBuffer,
					VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
					VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
					VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
					VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
					VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
					VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
					0,
					1,
					&attachment_load_barrier,
					0,
					nullptr,
					0,
					nullptr);
			}
			VkRenderingAttachmentInfoKHR color_attachments_info[k_max_image_outputs] = {};
			for (auto i = 0; i < renderTargetCount; i++)
			{
				VkAttachmentLoadOp color_op{};
				color_op = ash_load_operation_to_vk(renderPass->get_color_operations()[i]);
				VkRenderingAttachmentInfoKHR& color_attachment_info = color_attachments_info[i];
				color_attachment_info.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO_KHR;
				color_attachment_info.imageView = (VkImageView)colorAttachements[i]->get_default_rtv()->get_native_handle();
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
				depth_attachment_info.imageView = (VkImageView)depthStencilAttachment->get_default_rtv()->get_native_handle();
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
				shading_rate_info.imageView = (VkImageView)srattachment->get_default_rtv()->get_native_handle();
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
	auto VulkanCommandBuffer::cmd_end_render_pass() -> void
	{
		H_ASSERTLOG(state == ASH_Recording, " you need call begin() before recording any command ! ");
		if (currentBoundRenderPass == nullptr)
		{
			HLogWarning("end_render_pass : none renderpass are bound currently, do nothing and return !");
			return;
		}
		auto& colorAttachments = currentBoundFramebuffer->get_render_targets();
		auto count = colorAttachments.size();
		auto depthAttachment = currentBoundFramebuffer->get_depth_stencil();
		const bool dynamic_rendering =
			VulkanContext::get()->get_device_extension_enabled(DeviceExtensionAndFeaturesFlags::DynamicRendering);
		if (dynamic_rendering) {
			vkCmdEndRenderingKHR(vkCommandBuffer);
		}
		else {
			vkCmdEndRenderPass(vkCommandBuffer);
		}
		// Legacy render passes perform implicit final-layout transitions on end, so the tracker can move
		// directly to the declared final state here. Dynamic rendering does not; attachments stay in their
		// renderable layouts until an explicit post-pass barrier is submitted outside the rendering scope.
		for (auto i = 0; i < count; i++)
		{
			if (!colorAttachments[i])
			{
				HLogWarning("cmd_end_render_pass: framebuffer color attachment {} is null, skip resource state update.", i);
				continue;
			}
			colorAttachments[i]->set_resource_state(
				dynamic_rendering ?
				AshResourceState::RTV :
				currentBoundRenderPass->get_color_attachment_final_state(i));
		}
		if (depthAttachment != nullptr)
		{
			depthAttachment->set_resource_state(
				dynamic_rendering ?
				AshResourceState::DSVWrite :
				currentBoundRenderPass->get_depth_stencil_attachment_final_state());
		}
		currentBoundRenderPass = nullptr;
		currentBoundFramebuffer = nullptr;
	}
	auto VulkanCommandBuffer::cmd_bind_pipeline() -> void
	{
		H_ASSERTLOG(state == ASH_Recording, " you need call begin() before recording any command ! ");

	}
	auto VulkanCommandBuffer::cmd_set_viewport(const Viewport& viewport) -> void
	{
		H_ASSERTLOG(state == ASH_Recording, " you need call begin() before recording any command ! ");
		VkViewport vk_viewport{};
		vk_viewport.x = static_cast<float>(viewport.rect.x);
		vk_viewport.y = static_cast<float>(viewport.rect.y);
		vk_viewport.width = static_cast<float>(viewport.rect.width);
		vk_viewport.height = static_cast<float>(viewport.rect.height);
		vk_viewport.minDepth = viewport.min_depth;
		vk_viewport.maxDepth = viewport.max_depth;
		vkCmdSetViewport(vkCommandBuffer, 0, 1, &vk_viewport);
	}
	auto VulkanCommandBuffer::cmd_set_scissor(const Rect2DInt& scissor) -> void
	{
		H_ASSERTLOG(state == ASH_Recording, " you need call begin() before recording any command ! ");
		VkRect2D vk_scissor{};
		vk_scissor.offset = { scissor.x, scissor.y };
		vk_scissor.extent = { scissor.width, scissor.height };
		vkCmdSetScissor(vkCommandBuffer, 0, 1, &vk_scissor);
	}
	auto VulkanCommandBuffer::cmd_bind_vertex_buffers(uint32_t firstBinding, uint32_t bindingCount, std::shared_ptr<Buffer>* buffers, const uint64_t* offsets) -> void
	{
		H_ASSERTLOG(state == ASH_Recording, " you need call begin() before recording any command ! ");
		H_ASSERTLOG(buffers, "vertex buffer array is null");
		std::vector<VkBuffer> vk_buffers(bindingCount);
		std::vector<VkDeviceSize> vk_offsets(bindingCount);
		for (uint32_t i = 0; i < bindingCount; ++i)
		{
			H_ASSERTLOG(buffers[i], "vertex buffer is null");
			vk_buffers[i] = reinterpret_cast<VkBuffer>(buffers[i]->get_native_handle());
			const uint64_t base_offset = static_cast<uint64_t>(buffers[i]->get_global_offset());
			vk_offsets[i] = (offsets ? offsets[i] : 0ull) + (base_offset == UINT32_MAX ? 0ull : base_offset);
		}
		vkCmdBindVertexBuffers(vkCommandBuffer, firstBinding, bindingCount, vk_buffers.data(), vk_offsets.data());
	}
	auto VulkanCommandBuffer::cmd_bind_index_buffer(std::shared_ptr<Buffer> buffer, uint64_t offset, AshIndexType indexType) -> void
	{
		H_ASSERTLOG(state == ASH_Recording, " you need call begin() before recording any command ! ");
		H_ASSERTLOG(buffer, "index buffer is null");
		const uint64_t base_offset = static_cast<uint64_t>(buffer->get_global_offset());
		const VkIndexType vk_index_type = indexType == ASH_INDEX_TYPE_UINT16 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32;
		vkCmdBindIndexBuffer(vkCommandBuffer, reinterpret_cast<VkBuffer>(buffer->get_native_handle()), offset + (base_offset == UINT32_MAX ? 0ull : base_offset), vk_index_type);
	}
	auto VulkanCommandBuffer::cmd_draw(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance) -> void
	{
		H_ASSERTLOG(state == ASH_Recording, " you need call begin() before recording any command ! ");
		vkCmdDraw(vkCommandBuffer, vertexCount, instanceCount, firstVertex, firstInstance);
	}
	auto VulkanCommandBuffer::cmd_draw_indexed(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance) -> void
	{
		H_ASSERTLOG(state == ASH_Recording, " you need call begin() before recording any command ! ");
		vkCmdDrawIndexed(vkCommandBuffer, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
	}
	auto VulkanCommandBuffer::cmd_dispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) -> void
	{
		H_ASSERTLOG(state == ASH_Recording, " you need call begin() before recording any command ! ");
		vkCmdDispatch(vkCommandBuffer, groupCountX, groupCountY, groupCountZ);
	}
	auto VulkanCommandBuffer::cmd_copy_texture(std::shared_ptr<Texture> source, std::shared_ptr<Texture> destination) -> bool
	{
		H_ASSERTLOG(state == ASH_Recording, " you need call begin() before recording any command ! ");
		ASH_SAFE_EXECUTE_BEGIN(bResult);
		ASH_LOG_PROCESS_ERROR(source && destination);

		auto sourceTexture = std::static_pointer_cast<VulkanTexture>(source);
		auto destinationTexture = std::static_pointer_cast<VulkanTexture>(destination);
		ASH_LOG_PROCESS_ERROR(sourceTexture && destinationTexture);
		ASH_LOG_PROCESS_ERROR(!sourceTexture->is_sparse() && !destinationTexture->is_sparse());

		const TextureCreation& sourceCreation = sourceTexture->get_desciption();
		const TextureCreation& destinationCreation = destinationTexture->get_desciption();
		const bool copyCompatible =
			sourceCreation.type == destinationCreation.type &&
			sourceCreation.format == destinationCreation.format &&
			sourceCreation.width == destinationCreation.width &&
			sourceCreation.height == destinationCreation.height &&
			sourceCreation.depth == destinationCreation.depth &&
			sourceCreation.array_layer_count == destinationCreation.array_layer_count &&
			sourceCreation.mip_level_count == destinationCreation.mip_level_count &&
			sourceCreation.eSampleCount == destinationCreation.eSampleCount;
		ASH_LOG_PROCESS_ERROR(copyCompatible);

		bool bRetCode = cmd_transition_resource_state({ source, AshResourceState::CopySrc });
		ASH_LOG_PROCESS_ERROR(bRetCode);
		bRetCode = cmd_transition_resource_state({ destination, AshResourceState::CopyDst });
		ASH_LOG_PROCESS_ERROR(bRetCode);

		std::vector<VkImageCopy> copyRegions{};
		const bool is3DTexture = sourceCreation.type == Ash_Texture3D;
		const uint32_t layerCount = is3DTexture ? 1u : static_cast<uint32_t>(sourceCreation.array_layer_count);
		copyRegions.reserve(static_cast<size_t>(sourceCreation.mip_level_count) * layerCount);

		for (uint32_t mipLevel = 0; mipLevel < sourceCreation.mip_level_count; ++mipLevel)
		{
			const uint32_t mipWidth = std::max<uint32_t>(1u, static_cast<uint32_t>(sourceCreation.width) >> mipLevel);
			const uint32_t mipHeight = std::max<uint32_t>(1u, static_cast<uint32_t>(sourceCreation.height) >> mipLevel);
			const uint32_t mipDepth = is3DTexture ? std::max<uint32_t>(1u, static_cast<uint32_t>(sourceCreation.depth) >> mipLevel) : 1u;

			for (uint32_t layerIndex = 0; layerIndex < layerCount; ++layerIndex)
			{
				VkImageCopy copyRegion{};
				copyRegion.srcSubresource.aspectMask = sourceTexture->get_vk_aspect_flags();
				copyRegion.srcSubresource.mipLevel = mipLevel;
				copyRegion.srcSubresource.baseArrayLayer = is3DTexture ? 0u : layerIndex;
				copyRegion.srcSubresource.layerCount = 1u;
				copyRegion.dstSubresource.aspectMask = destinationTexture->get_vk_aspect_flags();
				copyRegion.dstSubresource.mipLevel = mipLevel;
				copyRegion.dstSubresource.baseArrayLayer = is3DTexture ? 0u : layerIndex;
				copyRegion.dstSubresource.layerCount = 1u;
				copyRegion.extent = { mipWidth, mipHeight, mipDepth };
				copyRegions.push_back(copyRegion);
			}
		}

		ASH_LOG_PROCESS_ERROR(!copyRegions.empty());
		vkCmdCopyImage(
			vkCommandBuffer,
			sourceTexture->get_vk_image(),
			VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			destinationTexture->get_vk_image(),
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			static_cast<uint32_t>(copyRegions.size()),
			copyRegions.data());

		ASH_SAFE_EXECUTE_END(bResult);
		return bResult;
	}
	auto VulkanCommandBuffer::cmd_update_sub_resource(std::shared_ptr<Buffer> pBuffer, uint32_t uOffset, uint32_t uSize, void* pData) -> bool
	{
		H_ASSERTLOG(state == ASH_Recording, " you need call begin() before recording any command ! ");
		const AshResourceAccessType accessType =
			pBuffer ? pBuffer->get_buffer_creation_info().access_type : AshResourceAccessType::ASH_RESOURCE_ACCESS_WRITE;
		ASH_SAFE_EXECUTE_BEGIN(bResult); 
		ASH_LOG_PROCESS_ERROR(pBuffer);
		const auto& bufferDesc = pBuffer->get_buffer_creation_info();
		ASH_LOG_PROCESS_ERROR(uSize > 0);
		ASH_LOG_PROCESS_ERROR((bufferDesc.access_type != AshResourceAccessType::ASH_RESOURCE_ACCESS_READ));
		if (pBuffer->is_dynamic())
		{
			
		}
		else
		{	
			if ((bufferDesc.access_type == AshResourceAccessType::ASH_RESOURCE_ACCESS_GPU_ONLY)) // use staging
			{
				bool bRetCode = cmd_transition_resource_state({ pBuffer, AshResourceState::CopyDst });
				if (!bRetCode)
				{
					HLogError(
						"VulkanCommandBuffer: failed to transition buffer '{}' to CopyDst before upload (offset={}, size={}).",
						pBuffer->get_name() ? pBuffer->get_name() : "UnnamedBuffer",
						uOffset,
						uSize);
				}
				ASH_LOG_PROCESS_ERROR(bRetCode);
				VkBuffer destBuffer = (VkBuffer)pBuffer->get_native_handle();
				if (!destBuffer)
				{
					HLogError(
						"VulkanCommandBuffer: destination VkBuffer handle is null for '{}' during upload.",
						pBuffer->get_name() ? pBuffer->get_name() : "UnnamedBuffer");
				}
				ASH_PROCESS_ERROR_EXIT(destBuffer);
				const bool canUseCmdUpdate =
					(uSize <= 65536u) &&
					((uOffset & 0x3u) == 0u) &&
					((uSize & 0x3u) == 0u);
				if (canUseCmdUpdate)
				{
					vkCmdUpdateBuffer(vkCommandBuffer, destBuffer, uOffset, uSize, pData);
				}
				else
				{
					auto pVulkanStaingBuffer = Ash_New_Shared<VulkanStagingBuffer>(uSize, nullptr, false);
					ASH_LOG_PROCESS_ERROR(pVulkanStaingBuffer);
					bRetCode = pVulkanStaingBuffer->map(0, uSize);
					ASH_LOG_PROCESS_ERROR(bRetCode);
					memory_copy(pVulkanStaingBuffer->get_mapped_memory(), pData, uSize);
					bRetCode = pVulkanStaingBuffer->unmap();
					ASH_LOG_PROCESS_ERROR(bRetCode);

					VkBufferCopy copyRegion{};
					copyRegion.dstOffset = uOffset;
					copyRegion.srcOffset = pVulkanStaingBuffer->get_buffer_offset();
					copyRegion.size = uSize;
					VkBuffer srcBuffer = pVulkanStaingBuffer->get_vkbuffer_handle();
					ASH_PROCESS_ERROR_EXIT(srcBuffer);
					vkCmdCopyBuffer(vkCommandBuffer, srcBuffer, destBuffer, 1, &copyRegion);
				}
			}
			else
			{
				auto pVulkanBuffer = std::static_pointer_cast<VulkanBuffer>(pBuffer);
				VmaAllocation pVMAllocation = pVulkanBuffer->get_vma_allocation();
				ASH_LOG_PROCESS_ERROR(pVMAllocation);
				void* pMapData = nullptr;
				bool bRetCode = VulkanContext::get()->vma_map_memory(pVMAllocation, &pMapData);
				pMapData = (char*)pMapData + uOffset;
				ASH_PROCESS_ERROR_EXIT(bRetCode && pMapData);
				memcpy(pMapData, pData, uSize);
				pVulkanBuffer->flush_mapped_range();
				VulkanContext::get()->vma_unmap_memory(pVMAllocation);
			}
		}
		ASH_SAFE_EXECUTE_END(bResult);
		if (!bResult)
		{
			HLogError(
				"VulkanCommandBuffer: cmd_update_sub_resource failed for '{}' (offset={}, size={}, access_type={}, dynamic={}).",
				pBuffer && pBuffer->get_name() ? pBuffer->get_name() : "UnnamedBuffer",
				uOffset,
				uSize,
				static_cast<uint32_t>(accessType),
				pBuffer ? pBuffer->is_dynamic() : false);
		}
		return bResult;
	}

	auto VulkanCommandBuffer::cmd_update_texture_sub_resource(std::shared_ptr<Texture> pTexture, const void* pData) -> bool
	{
		H_ASSERTLOG(state == ASH_Recording, " you need call begin() before recording any command ! ");
		ASH_SAFE_EXECUTE_BEGIN(bResult);
		ASH_LOG_PROCESS_ERROR(pTexture && pData);

		auto pVulkanTexture = std::static_pointer_cast<VulkanTexture>(pTexture);
		ASH_LOG_PROCESS_ERROR(pVulkanTexture);

		const TextureCreation& creation = pVulkanTexture->get_desciption();
		ASH_LOG_PROCESS_ERROR(creation.eSampleCount == ASH_SAMPLE_COUNT_1_BIT);
		ASH_LOG_PROCESS_ERROR(!pVulkanTexture->is_sparse());

		const AshTextureFormatInfo& formatInfo = get_vk_texture_format_info(creation.format);
		ASH_LOG_PROCESS_ERROR(formatInfo.vkFormat != VK_FORMAT_UNDEFINED);
		ASH_LOG_PROCESS_ERROR(!TextureFormat::has_depth_or_stencil(formatInfo.vkFormat));

		TextureUploadFormatInfo uploadFormatInfo{};
		uploadFormatInfo.bytesPerBlock = formatInfo.uBytesPerBlock;
		uploadFormatInfo.widthPerBlock = formatInfo.uWidthPerBlock;
		uploadFormatInfo.heightPerBlock = formatInfo.uHeightPerBlock;

		std::vector<TextureUploadSubresource> subresources{};
		uint64_t tightPackedBytes = 0;
		ASH_LOG_PROCESS_ERROR(build_tightly_packed_texture_upload_layout(creation, uploadFormatInfo, subresources, tightPackedBytes));
		ASH_LOG_PROCESS_ERROR(!subresources.empty() && tightPackedBytes > 0);

		const uint32_t offsetAlignment = std::lcm<uint32_t>(4u, std::max<uint32_t>(1u, formatInfo.uBytesPerBlock));
		auto align_up = [offsetAlignment](uint64_t value) -> uint64_t
			{
				const uint64_t alignment = static_cast<uint64_t>(offsetAlignment);
				return (value + alignment - 1ull) / alignment * alignment;
			};

		std::vector<uint64_t> stagingOffsets(subresources.size(), 0ull);
		uint64_t stagingSize = 0;
		for (size_t index = 0; index < subresources.size(); ++index)
		{
			stagingSize = align_up(stagingSize);
			stagingOffsets[index] = stagingSize;
			stagingSize += subresources[index].sourceSize;
		}

		ASH_LOG_PROCESS_ERROR(stagingSize > 0 && stagingSize <= UINT32_MAX);
		auto pVulkanStagingBuffer = Ash_New_Shared<VulkanStagingBuffer>(static_cast<uint32_t>(stagingSize), nullptr, false);
		ASH_LOG_PROCESS_ERROR(pVulkanStagingBuffer);

		bool bRetCode = pVulkanStagingBuffer->map(0, static_cast<uint32_t>(stagingSize));
		ASH_LOG_PROCESS_ERROR(bRetCode);
		uint8_t* mappedData = reinterpret_cast<uint8_t*>(pVulkanStagingBuffer->get_mapped_memory());
		ASH_PROCESS_ERROR_EXIT(mappedData);

		const uint8_t* sourceData = reinterpret_cast<const uint8_t*>(pData);
		for (size_t index = 0; index < subresources.size(); ++index)
		{
			const TextureUploadSubresource& subresource = subresources[index];
			std::memcpy(
				mappedData + stagingOffsets[index],
				sourceData + subresource.sourceOffset,
				static_cast<size_t>(subresource.sourceSize));
		}

		bRetCode = pVulkanStagingBuffer->unmap();
		ASH_LOG_PROCESS_ERROR(bRetCode);
		bRetCode = cmd_transition_resource_state({ pTexture, AshResourceState::CopyDst });
		ASH_LOG_PROCESS_ERROR(bRetCode);

		std::vector<VkBufferImageCopy> copyRegions(subresources.size());
		const VkDeviceSize sliceBaseOffset = pVulkanStagingBuffer->get_buffer_offset();
		for (size_t index = 0; index < subresources.size(); ++index)
		{
			const TextureUploadSubresource& subresource = subresources[index];
			VkBufferImageCopy& copyRegion = copyRegions[index];
			copyRegion.bufferOffset = sliceBaseOffset + stagingOffsets[index];
			copyRegion.bufferRowLength = 0;
			copyRegion.bufferImageHeight = 0;
			copyRegion.imageSubresource.aspectMask = pVulkanTexture->get_vk_aspect_flags();
			copyRegion.imageSubresource.mipLevel = subresource.mipLevel;
			copyRegion.imageSubresource.baseArrayLayer = creation.type == Ash_Texture3D ? 0u : subresource.arrayLayer;
			copyRegion.imageSubresource.layerCount = 1;
			copyRegion.imageExtent.width = subresource.width;
			copyRegion.imageExtent.height = subresource.height;
			copyRegion.imageExtent.depth = subresource.depth;
		}

		vkCmdCopyBufferToImage(
			vkCommandBuffer,
			pVulkanStagingBuffer->get_vkbuffer_handle(),
			pVulkanTexture->get_vk_image(),
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			static_cast<uint32_t>(copyRegions.size()),
			copyRegions.data());

		if (creation.initial_state != AshResourceState::Unknown && creation.initial_state != AshResourceState::CopyDst)
		{
			bRetCode = cmd_transition_resource_state({ pTexture, AshResourceState::CopyDst, creation.initial_state });
			ASH_LOG_PROCESS_ERROR(bRetCode);
		}

		ASH_SAFE_EXECUTE_END(bResult);
		return bResult;
	}
	auto VulkanCommandBuffer::cmd_transition_resource_state(const AshBarrier& barrierInfo) -> bool
	{
		ASH_SAFE_EXECUTE_BEGIN(bResult);
		bool bretCode = cmd_transition_resource_state(&barrierInfo, 1);
		ASH_SAFE_EXECUTE_END(bResult);
		return bResult;
	}
	auto VulkanCommandBuffer::cmd_transition_resource_state(const std::initializer_list<AshBarrier>& lsBarrierInfoArrray) -> bool
	{
		ASH_SAFE_EXECUTE_BEGIN(bResult);
		bool bretCode = cmd_transition_resource_state(lsBarrierInfoArrray.begin(), (uint32_t)lsBarrierInfoArrray.size());
		ASH_SAFE_EXECUTE_END(bResult);
		return bResult;
	}
	auto VulkanCommandBuffer::cmd_transition_resource_state(const AshBarrier* pBarrierInfo, uint32_t uBarrierCount) -> bool
	{
		ASH_SAFE_EXECUTE_BEGIN(bResult);
		VkPipelineStageFlags srcFinalStageMask = 0;
		VkPipelineStageFlags dstFinalStageMask = 0;
		VkMemoryBarrier memBarrier{};
		memBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
		memBarrier.pNext = nullptr;
		memBarrier.dstAccessMask = 0;
		memBarrier.srcAccessMask = 0;
		BOOL bMemBarrier = false;
		std::vector<VkImageMemoryBarrier> TexBarriers;
		std::vector<VkBufferMemoryBarrier> BufBarriers;
		uint32_t uMemBarriersCount = 0;
		uint32_t uTexBarriersCount = 0;
		uint32_t uBufBarriersCount = 0;
		for (uint32_t i = 0; i < uBarrierCount; i++)
		{
			auto& iter = pBarrierInfo[i];
			if (!iter.pTexture && !iter.pBuffer)
				continue;

			switch (iter.eType)
			{
			case AshBarrier::EType::Texture:
				++uTexBarriersCount;
				break;
			case AshBarrier::EType::Buffer:
				++uBufBarriersCount;
				break;
			default:
				break;
			}
		}
		if (uTexBarriersCount > 0)
			TexBarriers.reserve(uTexBarriersCount);

		if (uBufBarriersCount > 0)
			BufBarriers.reserve(uBufBarriersCount);
		for (uint32_t i = 0; i < uBarrierCount; i++)
		{
			auto& iter = pBarrierInfo[i];
			if (!iter.pTexture && !iter.pBuffer)
				continue;


			const AshSubresourceRange& DstSub = static_cast<AshSubresourceRange>(iter);
			uint32_t uUsageFlags = 0;
			VkPipelineStageFlags srcStageMask = 0;
			VkPipelineStageFlags dstStageMask = 0;
			VkAccessFlags srcAccessFlags = 0;
			VkAccessFlags dstAccessFlags = 0;
			VkImageLayout srcLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			VkImageLayout dstLayout = VK_IMAGE_LAYOUT_UNDEFINED;

			std::shared_ptr<VulkanBuffer> pBuffer = nullptr;
			std::shared_ptr<VulkanTexture> pTexture = nullptr;

			bool bTexR64 = false;
			VkImageAspectFlags aspectMask;

			switch (iter.eType)
			{
			case AshBarrier::EType::Buffer:
			{
				pBuffer = std::static_pointer_cast<VulkanBuffer>(iter.pBuffer);
				ASH_LOG_PROCESS_ERROR(pBuffer);
				uUsageFlags = pBuffer->get_buffer_creation_info().usage_flags;
			}
			break;
			case AshBarrier::EType::Texture:
			{
				pTexture = std::static_pointer_cast<VulkanTexture>(iter.pTexture);
				uUsageFlags = pTexture->get_desciption().uUsageFlags;
				if (pTexture->get_desciption().format == ASH_FORMAT_R64_UINT)
				{
					bTexR64 = true;
				}
				aspectMask = pTexture->get_vk_aspect_flags();
			}
			break;
			
			case AshBarrier::EType::Unknown:
				assert(false);
				break;
			default:
				assert(false);
				break;

			}

			if (pTexture)
			{
				AshResourceState srcAccess = AshResourceState::Unknown;
				bool bHasLastTextureBarrier = false;
				const bool bIsDepthStencil = (uUsageFlags & ASH_TEXTURE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0;
				const auto& texDesc = pTexture->get_desciption();
				VkImage vkTextureImage = pTexture->get_vk_image();
				auto& TextureLayoutTracker = pTexture->get_resource_tracker();

				//collect specific mip/slice subresource
				//collect all range subresource when mip == -1 && slice == -1
				auto TextureMipBarrierCollection = [&](uint32_t Mip, uint32_t Slice, AshResourceState InSrcAccess, AshResourceState InDstAccess)
					{
						if (!bTexR64)
						{
							get_vk_stage_and_access_flags(InSrcAccess, iter.eType, uUsageFlags, bIsDepthStencil, srcStageMask, srcAccessFlags, srcLayout, true);
							get_vk_stage_and_access_flags(InDstAccess, iter.eType, uUsageFlags, bIsDepthStencil, dstStageMask, dstAccessFlags, dstLayout, false);
						}
						else
						{
							get_vk_stage_and_access_flags(has_any_flags((uint32_t)InSrcAccess, (uint32_t)AshResourceState::SRVMask) ? AshResourceState::UAVMask : InSrcAccess, iter.eType, uUsageFlags, bIsDepthStencil, srcStageMask, srcAccessFlags, srcLayout, true);
							get_vk_stage_and_access_flags(has_any_flags((uint32_t)InDstAccess, (uint32_t)AshResourceState::SRVMask) ? AshResourceState::UAVMask : InDstAccess, iter.eType, uUsageFlags, bIsDepthStencil, dstStageMask, dstAccessFlags, dstLayout, false);
						}

						// If we're not transitioning across pipes and we don't need to perform layout transitions, we can express memory dependencies through a global memory barrier.
						if (srcLayout == dstLayout)
						{
							// We only need a memory barrier if the previous commands wrote to the buffer. In case of a transition from read, an execution barrier is enough.
							const bool bSrcAccessIsRead = is_vk_access_read_only(srcAccessFlags);
							if (Mip == (uint32_t)-1 && Slice == (uint32_t)-1)
							{
								pTexture->get_resource_tracker().clear_subresource_state();
								pTexture->get_resource_tracker().set_all_resource_state(InDstAccess);
							}
							else
							{
								pTexture->get_resource_tracker().set_texture_subresource_state(InDstAccess, Mip + texDesc.mip_level_count * Slice);
							}

							if (!bSrcAccessIsRead)
							{
								srcFinalStageMask |= srcStageMask;
								dstFinalStageMask |= dstStageMask;
								memBarrier.srcAccessMask |= srcAccessFlags;
								memBarrier.dstAccessMask |= dstAccessFlags;
								bMemBarrier = true;
							}
							return;
						}

						if (Mip == (uint32_t)-1 && Slice == (uint32_t)-1)
						{
							// Transition the whole texture with a single image barrier.
							srcFinalStageMask |= srcStageMask;
							dstFinalStageMask |= dstStageMask;
							pTexture->get_resource_tracker().clear_subresource_state();
							pTexture->get_resource_tracker().set_all_resource_state(InDstAccess);

							VkImageSubresourceRange AllSubresourceRange{};
							AllSubresourceRange.aspectMask = aspectMask;

							AllSubresourceRange.baseMipLevel = 0;
							AllSubresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
							AllSubresourceRange.baseArrayLayer = 0;
							AllSubresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;

							VkImageMemoryBarrier imageBarrier{};
							imageBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
							imageBarrier.pNext = nullptr;
							imageBarrier.srcAccessMask = srcAccessFlags;
							imageBarrier.dstAccessMask = dstAccessFlags;
							imageBarrier.oldLayout = srcLayout;
							imageBarrier.newLayout = dstLayout;
							imageBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
							imageBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
							imageBarrier.image = vkTextureImage;
							imageBarrier.subresourceRange = AllSubresourceRange;

							TexBarriers.emplace_back(imageBarrier);
						}
						else
						{
							srcFinalStageMask |= srcStageMask;
							dstFinalStageMask |= dstStageMask;
							pTexture->get_resource_tracker().set_texture_subresource_state(InDstAccess, Mip + texDesc.mip_level_count* Slice);

							bool bNewBarrier = true;
							if (bHasLastTextureBarrier)
							{
								auto& LastBarrier = TexBarriers.back();
								if (
									srcLayout == LastBarrier.oldLayout &&
									dstLayout == LastBarrier.newLayout &&
									LastBarrier.subresourceRange.baseArrayLayer == Slice &&
									LastBarrier.subresourceRange.levelCount + LastBarrier.subresourceRange.baseMipLevel == Mip &&
									LastBarrier.image == vkTextureImage
									)
								{
									++LastBarrier.subresourceRange.levelCount;
									bNewBarrier = false;
								}
							}
							if (bNewBarrier)
							{
								VkImageSubresourceRange subresourceRange{};
								subresourceRange.aspectMask = aspectMask;
								subresourceRange.baseMipLevel = Mip;
								subresourceRange.levelCount = 1;
								subresourceRange.baseArrayLayer = Slice;
								subresourceRange.layerCount = 1;
								VkImageMemoryBarrier imageBarrier{};
								imageBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
								imageBarrier.pNext = nullptr;
								imageBarrier.srcAccessMask = srcAccessFlags;
								imageBarrier.dstAccessMask = dstAccessFlags;
								imageBarrier.oldLayout = srcLayout;
								imageBarrier.newLayout = dstLayout;
								imageBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
								imageBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
								imageBarrier.image = vkTextureImage;
								imageBarrier.subresourceRange = subresourceRange;
								TexBarriers.emplace_back(imageBarrier);
								bHasLastTextureBarrier = true;
							}
						}
					};

				bool bDstAllResourceTransition = DstSub.IsWholeResource() || (DstSub.uArrayCount == texDesc.array_layer_count && DstSub.uMipCount == texDesc.mip_level_count);
				if (bDstAllResourceTransition)
				{
					bool bHasSubResourceTransition = TextureLayoutTracker.has_subresource_transition();
					if (!bHasSubResourceTransition)
					{
						srcAccess = iter.eSRCAccess == AshResourceState::Unknown ? TextureLayoutTracker.get_all_resource_state() : iter.eSRCAccess;
						if (has_any_flags((uint32_t)srcAccess, (uint32_t)AshResourceState::UAVMask) && has_any_flags((uint32_t)iter.eDSTAccess, (uint32_t)AshResourceState::UAVMask))
						{
							continue;
						}

						TextureMipBarrierCollection((uint32_t)-1, (uint32_t)-1, srcAccess, iter.eDSTAccess);
					}
					else
					{
						TextureLayoutTracker.traverse_texture_all_subresource(pTexture, iter.eSRCAccess, iter.eDSTAccess, TextureMipBarrierCollection);
						TextureLayoutTracker.clear_subresource_state();
						pTexture->get_resource_tracker().set_all_resource_state(iter.eDSTAccess);
					}
				}
				else
				{
					TextureLayoutTracker.traverse_texture_subresource(pTexture, DstSub, iter.eSRCAccess, iter.eDSTAccess, TextureMipBarrierCollection);
				}
			}

			if (pBuffer)
			{
				AshResourceState srcAccess = iter.eSRCAccess == AshResourceState::Unknown ? pBuffer->get_resource_tracker().get_all_resource_state() : iter.eSRCAccess;

				if (has_any_flags((uint32_t)srcAccess, (uint32_t)AshResourceState::UAVMask) && has_any_flags((uint32_t)iter.eDSTAccess, (uint32_t)AshResourceState::UAVMask))
				{
					continue;
				}

				get_vk_stage_and_access_flags(srcAccess, iter.eType, uUsageFlags, false, srcStageMask, srcAccessFlags, srcLayout, true);
				get_vk_stage_and_access_flags(iter.eDSTAccess, iter.eType, uUsageFlags, false, dstStageMask, dstAccessFlags, dstLayout, false);

				// If we're not transitioning across pipes and we don't need to perform layout transitions, we can express memory dependencies through a global memory barrier.
				if (srcLayout == dstLayout)
				{
					// We only need a memory barrier if the previous commands wrote to the buffer. In case of a transition from read, an execution barrier is enough.
					const bool bSrcAccessIsRead = is_vk_access_read_only(srcAccessFlags);
					pBuffer->get_resource_tracker().set_all_resource_state(iter.eDSTAccess);

					if (!bSrcAccessIsRead)
					{
						srcFinalStageMask |= srcStageMask;
						dstFinalStageMask |= dstStageMask;
						memBarrier.srcAccessMask |= srcAccessFlags;
						memBarrier.dstAccessMask |= dstAccessFlags;
						bMemBarrier = true;
					}
					continue;
				}

				srcFinalStageMask |= srcStageMask;
				dstFinalStageMask |= dstStageMask;
				VkBufferMemoryBarrier bufferBarrier{};
				bufferBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
				bufferBarrier.pNext = nullptr;
				bufferBarrier.srcAccessMask = srcAccessFlags;
				bufferBarrier.dstAccessMask = dstAccessFlags;
				bufferBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				bufferBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
				bufferBarrier.buffer = pBuffer->get_vk_buffer_handle();
				bufferBarrier.offset = 0;
				bufferBarrier.size = VK_WHOLE_SIZE;

				BufBarriers.emplace_back(bufferBarrier);
				pBuffer->get_resource_tracker().set_all_resource_state(iter.eDSTAccess);
				continue;
			}
		}
		if (bMemBarrier || !BufBarriers.empty() || !TexBarriers.empty())
		{
			if (srcFinalStageMask == 0)
			{
				srcFinalStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
			}
			if (dstFinalStageMask == 0)
			{
				dstFinalStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
			}
			vkCmdPipelineBarrier(vkCommandBuffer,
				srcFinalStageMask,
				dstFinalStageMask,
				0,
				(bMemBarrier ? 1 : 0), (bMemBarrier ? &memBarrier : nullptr),
				(uint32_t)BufBarriers.size(), !BufBarriers.empty() ? BufBarriers.data() : nullptr,
				(uint32_t)TexBarriers.size(), !TexBarriers.empty() ? TexBarriers.data() : nullptr
			);
		}
		ASH_SAFE_EXECUTE_END(bResult);
		return bResult;
	}
	auto VulkanCommandBuffer::set_state(AshCommandBufferState _state) -> void
	{
		state = _state;
	}
}
