#pragma once
#include "Base/hplatform.h"
#include "Graphics/RHICommon.h"
#include "VulkanHelper.hpp"
#include "SpvHelper.hpp"
#include <memory>
using namespace AshEngine;
namespace RHI
{
	struct ShaderStateCreation;
	//internal class none rhi
	class VulkanShader
	{
	public:
		VulkanShader(const ShaderStateCreation& ci);
		~VulkanShader();
		static auto create(const ShaderStateCreation& ci) -> std::shared_ptr<VulkanShader>;
	private:
		const char* name = nullptr;
		VkPipelineShaderStageCreateInfo				shader_stage_info[k_max_shader_stages];
		VkRayTracingShaderGroupCreateInfoKHR		shader_group_info[k_max_shader_stages];
		uint32_t									active_shaders = 0;
		bool										graphics_pipeline = false;
		bool										ray_tracing_pipeline = false;
	};
}