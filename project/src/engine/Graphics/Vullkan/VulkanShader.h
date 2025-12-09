#pragma once
#include "Base/hplatform.h"
#include "Graphics/RHICommon.h"
#include "Graphics/Shader.h"
#include "VulkanHelper.hpp"
#include "SpvHelper.h"
#include <memory>
using namespace AshEngine;
namespace RHI
{
	static const uint32_t				 k_max_name_length = 32;
	static const uint32_t				 k_max_path_length = 128;
	static const uint32_t                k_max_count = 128;
	static const uint32_t                k_max_push_constants_count = 1;
	static const uint32_t                k_max_specialization_constants = 4;
	typedef struct AshBufferMemberInfo
	{
		uint32_t			size = 0;
		uint32_t			offset = 0;
		AshShaderDataType	type = ASH_None;
		char				name[k_max_name_length];
		char				fullName[k_max_name_length];
	}AshBufferMemberInfo;
	//for reason that to support both dx12 and vulkan , we'd better give up the push-constant for which is only for vulkan.
	//also specialization-const for the same reason.

	struct ShaderCreation;
	//internal class none rhi
	class VulkanShader : public Shader
	{
	public:
		VulkanShader(const ShaderCreation& ci);
		~VulkanShader();
	private:
		char name[k_max_name_length];
		char path[k_max_path_length];
		VkPipelineShaderStageCreateInfo				shader_stage_info[k_max_shader_stages];
		VkRayTracingShaderGroupCreateInfoKHR		shader_group_info[k_max_shader_stages];
		uint32_t									active_shaders = 0;
		bool										graphics_pipeline = false;
		bool										ray_tracing_pipeline = false;
		VkShaderModuleCreateInfo					m_shader_create_info = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
		// Í¨¹ý Shader ¼Ì³Ð
		auto get_native_handle() -> void* override;
		auto get_name() -> const char* override;
	};
}