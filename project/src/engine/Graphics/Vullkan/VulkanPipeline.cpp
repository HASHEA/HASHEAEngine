#include "VulkanPipeline.h"
namespace RHI
{
	VulkanPipeline::VulkanPipeline(const PipelineCreation& ci)
	{
		name = ci.name;
		VkPipelineCache pipeline_cache = VK_NULL_HANDLE;
		VkPipelineCacheCreateInfo pipeline_cache_create_info{ VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO };
		
	}
	VulkanPipeline::~VulkanPipeline()
	{
	}
}