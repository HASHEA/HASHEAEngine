#pragma once
#include "Graphics/Pipeline.h"
#include "VulkanHelper.hpp"
namespace RHI
{
	class VulkanPipeline
	{
	public:
		VulkanPipeline(const PipelineCreation& ci);
		~VulkanPipeline();

	private:
		const char*						name				= nullptr;
		VkPipeline                      vk_pipeline			= VK_NULL_HANDLE;
		VkPipelineLayout                vk_pipeline_layout	= VK_NULL_HANDLE;
		VkPipelineBindPoint             vk_bind_point{};
	};
}