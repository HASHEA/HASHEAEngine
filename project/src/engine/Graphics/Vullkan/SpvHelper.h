#pragma once
#include "Graphics/Pipeline.h"
#include "Base/hassert.h"
#include <string>
#include <vector>

namespace RHI
{
	static const uint32_t k_max_reflected_descriptor_sets = 8;
	static const uint32_t k_max_push_constants_count = 1;
	static const uint32_t k_max_specialization_constants = 4;
	static const uint32_t k_max_reflected_buffer_members = 32;
	static const uint32_t k_default_bindless_descriptor_count = 1024;

	struct ConstantValue
	{
		enum class Type : uint8_t
		{
			Type_i32 = 0,
			Type_u32,
			Type_f32,
			Type_count
		};

		union Value
		{
			int32_t value_i;
			uint32_t value_u;
			float value_f;
		};

		Value value{};
		Type type = Type::Type_u32;
	};

	struct SpecializationConstant
	{
		uint16_t binding = 0;
		uint16_t byte_stride = 0;
		ConstantValue default_value{};
	};

	struct SpecializationName
	{
		char name[32]{};
	};

	struct AshBufferMemberInfo
	{
		uint32_t size = 0;
		uint32_t offset = 0;
		AshShaderDataType type = ASH_None;
		char name[32]{};
		char fullName[64]{};
	};

	struct DescriptorSetLayoutCreation
	{
		struct Binding
		{
			AshDescriptorType type = ASH_DESCRIPTOR_TYPE_MAX_ENUM;
			uint16_t index = 0;
			uint16_t count = 0;
			AshShaderStageFlagBits stage_flags = ASH_SHADER_STAGE_FLAG_BITS_MAX_ENUM;
			bool bindless = false;
			char name[64]{};
		};

		Binding bindings[k_max_descriptors_per_set]{};
		uint32_t num_bindings = 0;
		uint32_t set_index = 0;
		bool bindless = false;
		bool dynamic = false;
		char name[64]{};

		inline DescriptorSetLayoutCreation& add_binding(const Binding& binding)
		{
			H_ASSERT(num_bindings < k_max_descriptors_per_set);
			bindings[num_bindings++] = binding;
			return *this;
		}

		inline DescriptorSetLayoutCreation& set_set_index(uint32_t index)
		{
			set_index = index;
			return *this;
		}
	};

	struct ComputeLocalSize
	{
		uint32_t x = 1;
		uint32_t y = 1;
		uint32_t z = 1;
	};

	struct PipelineReflectionInfo
	{
		AshShaderStageFlagBits active_stages = static_cast<AshShaderStageFlagBits>(0);
		VertexInputCreation vertex_input{};
		ComputeLocalSize compute_local_size{};
	};

	struct ParseResult
	{
		uint32_t set_count = 0;
		uint32_t specialization_constants_count = 0;
		uint32_t push_constants_count = 0;
		uint32_t push_constants_stride = 0;

		DescriptorSetLayoutCreation sets[k_max_reflected_descriptor_sets]{};
		SpecializationConstant specialization_constants[k_max_specialization_constants]{};
		SpecializationName specialization_names[k_max_specialization_constants]{};
		AshBufferMemberInfo push_constant_members[k_max_reflected_buffer_members]{};
		uint32_t push_constant_member_count = 0;
		PipelineReflectionInfo pipeline_info{};
	};

	bool parse_binary_spv(const uint32_t* data, size_t word_count, AshShaderStageFlagBits stage, ParseResult* parse_result);
	bool merge_parse_results(const ParseResult* parse_results, uint32_t count, ParseResult* merged_result);
}
