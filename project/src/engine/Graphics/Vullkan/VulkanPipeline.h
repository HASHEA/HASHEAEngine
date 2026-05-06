#pragma once
#include "Graphics/Pipeline.h"
#include "SpvHelper.h"
#include "VulkanHelper.hpp"
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
namespace RHI
{
	class VulkanPipeline
	{
	public:
		VulkanPipeline(const PipelineCreation& ci, const std::unordered_map<std::string, ConstantValue>* specialization_overrides = nullptr);
		~VulkanPipeline();

		bool is_valid() const;
		VkPipeline get_native_handle() const;
		VkPipelineLayout get_pipeline_layout() const;
		VkPipelineBindPoint get_bind_point() const;
	
	private:
		std::string						name_storage;
		const char*						name				= nullptr;
		VkPipeline                      vk_pipeline			= VK_NULL_HANDLE;
		VkPipelineLayout                vk_pipeline_layout	= VK_NULL_HANDLE;
		VkPipelineBindPoint             vk_bind_point{};
		std::vector<std::shared_ptr<DescriptorSetLayout>> m_descriptorSetLayouts;
	};
}
