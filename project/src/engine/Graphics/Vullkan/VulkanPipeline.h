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
		VkPipeline                      vk_pipeline;
		VkPipelineLayout                vk_pipeline_layout;
		VkPipelineBindPoint             vk_bind_point;
	};
}