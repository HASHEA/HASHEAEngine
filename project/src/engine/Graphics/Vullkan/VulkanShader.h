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

	typedef struct AshPushContantDescription
	{
		uint32_t				size = 0;
		char					name[k_max_name_length];
		uint32_t				memberCount = 0;
		AshBufferMemberInfo		memberInfos[k_max_count];
	}AshPushContantDescription;

	struct ConstantValue {

		enum class Type : uint8_t {
			Type_i32 = 0,
			Type_u32,
			Type_f32,
			Type_count
		}; // enum Type

		union Value {

			int32_t                         value_i;
			uint32_t                         value_u;
			float                         value_f;
		}; // union Value

		Value                           value;
		Type                            type;
	}; // struct ConstantValue

	struct SpecializationConstant {

		uint16_t							binding = 0;
		uint16_t							byte_stride = 0;
		ConstantValue						default_value;
	}; // struct SpecializationConstant

	struct SpecializationName {
		char                        name[k_max_name_length];
	}; // struct SpecializationName

	struct ComputeLocalSize {

		uint32_t                             x : 10;
		uint32_t                             y : 10;
		uint32_t                             z : 10;
		uint32_t                             pad : 2;
	}; // struct ComputeLocalSize
	struct DescriptorSetLayoutCreation {
	private:
		uint32_t						num_bindings = 0;
	public:
		struct Binding {

			AshDescriptorType				type = ASH_DESCRIPTOR_TYPE_MAX_ENUM;
			uint16_t                        index = 0;
			uint16_t                        count = 0;
			const char* name = nullptr;  // Comes from external memory.
		}; // struct Binding

		Binding                         bindings[k_max_descriptors_per_set];

		uint32_t                        set_index = 0;
		bool                            bindless = false;
		bool                            dynamic = false;

		const char* name = nullptr;
		// Building helpers
		inline DescriptorSetLayoutCreation& add_binding(const Binding& binding)
		{
			H_ASSERT(!bindless);
			bindings[num_bindings++] = binding;
			return *this;
		}
		inline DescriptorSetLayoutCreation& add_binding(AshDescriptorType type, uint32_t index, uint32_t count, const char* name)
		{
			H_ASSERT(!bindless);
			bindings[num_bindings++] = { type, (uint16_t)index, (uint16_t)count, name };
			return *this;
		}
		inline DescriptorSetLayoutCreation& add_binding_at_index(const Binding& binding, int index)
		{
			H_ASSERT(!bindless);
			bindings[index] = binding;
			num_bindings = (index + 1) > num_bindings ? (index + 1) : num_bindings;
			return *this;
		}
		inline DescriptorSetLayoutCreation& set_set_index(uint32_t index)
		{
			set_index = index;
			return *this;
		}
		inline DescriptorSetLayoutCreation& add_binding_bindless(AshDescriptorType type, uint32_t index, const char* name)
		{
			if (num_bindings > 0)
			{
				H_ASSERTLOG(bindless, "the set bound with bindless binding is not a bindless set !");
			}
			bindless = true;
			bindings[num_bindings++] = { type, (uint16_t)index, (uint16_t)0, name };
			return *this;
		}

	}; // struct DescriptorSetLayoutCreation
	struct ParseResult {
		uint32_t								set_count = 0;
		uint32_t								specialization_constants_count = 0;
		uint32_t								push_constants_count = 0;
		uint32_t								intputs_count = 0;
		AshPushContantDescription				pushConsts[k_max_push_constants_count];
		DescriptorSetLayoutCreation				sets[k_max_count];
		AshInputAttributeDescription			inputs[k_max_count];
		SpecializationConstant					specialization_constants[k_max_specialization_constants];
		SpecializationName						specialization_names[k_max_specialization_constants];
		ComputeLocalSize						compute_local_size;
	}; // struct ParseResult

	struct ShaderCreation;
	//internal class none rhi
	class VulkanShader : public Shader
	{
	public:
		VulkanShader(const ShaderCreation& ci);
		~VulkanShader();
		static auto create(const ShaderCreation& ci) -> std::shared_ptr<VulkanShader>;

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