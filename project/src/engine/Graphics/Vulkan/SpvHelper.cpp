#include "SpvHelper.h"
#include "Base/hlog.h"
#include "spirv.hpp"
#include "spirv_cross.hpp"
#include <algorithm>
#include <cstring>

namespace RHI
{
	static void copy_name(char* dst, size_t dst_size, const std::string& src)
	{
		if (!dst || dst_size == 0)
		{
			return;
		}
		const size_t copy_size = std::min(dst_size - 1, src.size());
		if (copy_size > 0)
		{
			memcpy(dst, src.data(), copy_size);
		}
		dst[copy_size] = '\0';
	}

	static AshShaderStageFlagBits spirv_execution_model_to_ash(spv::ExecutionModel model)
	{
		switch (model)
		{
		case spv::ExecutionModelVertex:
			return ASH_SHADER_STAGE_VERTEX_BIT;
		case spv::ExecutionModelTessellationControl:
			return ASH_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
		case spv::ExecutionModelTessellationEvaluation:
			return ASH_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
		case spv::ExecutionModelGeometry:
			return ASH_SHADER_STAGE_GEOMETRY_BIT;
		case spv::ExecutionModelFragment:
			return ASH_SHADER_STAGE_FRAGMENT_BIT;
		case spv::ExecutionModelGLCompute:
			return ASH_SHADER_STAGE_COMPUTE_BIT;
		case spv::ExecutionModelRayGenerationKHR:
			return ASH_SHADER_STAGE_RAYGEN_BIT_KHR;
		case spv::ExecutionModelIntersectionKHR:
			return ASH_SHADER_STAGE_INTERSECTION_BIT_KHR;
		case spv::ExecutionModelAnyHitKHR:
			return ASH_SHADER_STAGE_ANY_HIT_BIT_KHR;
		case spv::ExecutionModelClosestHitKHR:
			return ASH_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
		case spv::ExecutionModelMissKHR:
			return ASH_SHADER_STAGE_MISS_BIT_KHR;
		case spv::ExecutionModelCallableKHR:
			return ASH_SHADER_STAGE_CALLABLE_BIT_KHR;
		case spv::ExecutionModelTaskEXT:
			return ASH_SHADER_STAGE_TASK_BIT_EXT;
		case spv::ExecutionModelMeshEXT:
			return ASH_SHADER_STAGE_MESH_BIT_EXT;
		default:
			return ASH_SHADER_STAGE_FLAG_BITS_MAX_ENUM;
		}
	}

	static AshDescriptorType spirv_resource_type_to_ash_descriptor_type(const spirv_cross::Resource& resource, AshDescriptorType fallback_type)
	{
		(void)resource;
		return fallback_type;
	}

	static uint32_t get_resource_descriptor_count(spirv_cross::Compiler& compiler, const spirv_cross::SPIRType& type, bool& bindless)
	{
		uint32_t descriptor_count = 1;
		bindless = false;
		for (size_t i = 0; i < type.array.size(); ++i)
		{
			uint32_t array_size = type.array[i];
			if (array_size == 0)
			{
				bindless = true;
				array_size = k_default_bindless_descriptor_count;
			}
			else if (!type.array_size_literal.empty() && i < type.array_size_literal.size() && !type.array_size_literal[i])
			{
				try
				{
					array_size = compiler.get_constant(array_size).m.c[0].r[0].u32;
				}
				catch (...)
				{
					array_size = 1;
				}
			}
			descriptor_count *= std::max(array_size, 1u);
		}
		return descriptor_count;
	}

	static AshShaderDataType spirv_type_to_shader_data_type(const spirv_cross::SPIRType& type)
	{
		if (type.columns > 1)
		{
			if (type.basetype == spirv_cross::SPIRType::Float && type.vecsize == 4 && type.columns == 4)
			{
				return ASH_Mat4;
			}
			return ASH_Struct;
		}

		switch (type.basetype)
		{
		case spirv_cross::SPIRType::Float:
			switch (type.vecsize)
			{
			case 1: return ASH_Float32;
			case 2: return ASH_Vec2;
			case 3: return ASH_Vec3;
			case 4: return ASH_Vec4;
			default: break;
			}
			break;
		case spirv_cross::SPIRType::Int:
			switch (type.vecsize)
			{
			case 1: return ASH_Int32;
			case 2: return ASH_IVec2;
			case 3: return ASH_IVec3;
			case 4: return ASH_IVec4;
			default: break;
			}
			break;
		case spirv_cross::SPIRType::UInt:
			return ASH_UInt;
		case spirv_cross::SPIRType::Boolean:
			return ASH_Bool;
		default:
			break;
		}
		return ASH_Struct;
	}

	static AshVertexComponentFormat spirv_type_to_vertex_format(const spirv_cross::SPIRType& type)
	{
		switch (type.basetype)
		{
		case spirv_cross::SPIRType::Float:
			switch (type.vecsize)
			{
			case 1: return Float;
			case 2: return Float2;
			case 3: return Float3;
			case 4: return Float4;
			default: break;
			}
			break;
		case spirv_cross::SPIRType::UInt:
		case spirv_cross::SPIRType::Int:
			switch (type.vecsize)
			{
			case 1: return Uint;
			case 2: return Uint2;
			case 4: return Uint4;
			default: break;
			}
			break;
		default:
			break;
		}
		return FormatCount;
	}

	static uint32_t get_vertex_format_size(AshVertexComponentFormat format)
	{
		switch (format)
		{
		case Float: return 4;
		case Float2: return 8;
		case Float3: return 12;
		case Float4: return 16;
		case Mat4: return 64;
		case Byte: return 1;
		case Byte4N: return 4;
		case UByte: return 1;
		case UByte4N: return 4;
		case Short2: return 4;
		case Short2N: return 4;
		case Short4: return 8;
		case Short4N: return 8;
		case Uint: return 4;
		case Uint2: return 8;
		case Uint4: return 16;
		default: return 0;
		}
	}

	void finalize_vertex_input_layout(VertexInputCreation& vertex_input)
	{
		if (vertex_input.num_vertex_attributes == 0)
		{
			return;
		}

		std::sort(vertex_input.vertex_attributes, vertex_input.vertex_attributes + vertex_input.num_vertex_attributes,
			[](const VertexAttribute& lhs, const VertexAttribute& rhs) { return lhs.location < rhs.location; });

		uint32_t stride = 0;
		for (uint32_t i = 0; i < vertex_input.num_vertex_attributes; ++i)
		{
			VertexAttribute& attribute = vertex_input.vertex_attributes[i];
			attribute.binding = 0;
			attribute.offset = stride;
			stride += get_vertex_format_size(attribute.format);
		}

		vertex_input.num_vertex_streams = 1;
		vertex_input.vertex_streams[0].binding = 0;
		vertex_input.vertex_streams[0].stride = static_cast<uint16_t>(stride);
		vertex_input.vertex_streams[0].input_rate = PerVertex;
	}

	static DescriptorSetLayoutCreation* find_or_add_set(ParseResult& parse_result, uint32_t set_index)
	{
		for (uint32_t i = 0; i < parse_result.set_count; ++i)
		{
			if (parse_result.sets[i].set_index == set_index)
			{
				return &parse_result.sets[i];
			}
		}

		H_ASSERT(parse_result.set_count < k_max_reflected_descriptor_sets);
		DescriptorSetLayoutCreation& set = parse_result.sets[parse_result.set_count++];
		set = DescriptorSetLayoutCreation{};
		set.set_set_index(set_index);
		return &set;
	}

	static void add_binding_if_unique(DescriptorSetLayoutCreation& creation, const DescriptorSetLayoutCreation::Binding& binding)
	{
		for (uint32_t i = 0; i < creation.num_bindings; ++i)
		{
			DescriptorSetLayoutCreation::Binding& existing = creation.bindings[i];
			if (existing.index == binding.index)
			{
				H_ASSERT(existing.type == binding.type);
				existing.stage_flags = static_cast<AshShaderStageFlagBits>(existing.stage_flags | binding.stage_flags);
				existing.count = std::max(existing.count, binding.count);
				existing.bindless = existing.bindless || binding.bindless;
				return;
			}
		}

		creation.add_binding(binding);
	}

	static void reflect_descriptor_resource(spirv_cross::Compiler& compiler, ParseResult& parse_result,
		const spirv_cross::Resource& resource, AshDescriptorType descriptor_type, AshShaderStageFlagBits stage)
	{
		uint32_t set_index = compiler.get_decoration(resource.id, spv::DecorationDescriptorSet);
		uint32_t binding_index = compiler.get_decoration(resource.id, spv::DecorationBinding);
		const spirv_cross::SPIRType& type = compiler.get_type(resource.type_id);
		bool bindless = false;
		uint32_t descriptor_count = get_resource_descriptor_count(compiler, type, bindless);

		DescriptorSetLayoutCreation::Binding binding{};
		binding.type = spirv_resource_type_to_ash_descriptor_type(resource, descriptor_type);
		binding.index = static_cast<uint16_t>(binding_index);
		binding.count = static_cast<uint16_t>(std::min<uint32_t>(descriptor_count, UINT16_MAX));
		binding.stage_flags = stage;
		binding.bindless = bindless;
		copy_name(binding.name, sizeof(binding.name), compiler.get_name(resource.id));

		DescriptorSetLayoutCreation* set = find_or_add_set(parse_result, set_index);
		set->bindless = set->bindless || bindless;
		add_binding_if_unique(*set, binding);
	}

	static void reflect_vertex_inputs(spirv_cross::Compiler& compiler, const spirv_cross::SmallVector<spirv_cross::Resource>& stage_inputs, ParseResult& parse_result)
	{
		auto& vertex_input = parse_result.pipeline_info.vertex_input;
		for (const auto& input : stage_inputs)
		{
			if (vertex_input.num_vertex_attributes >= k_max_vertex_attributes)
			{
				HLogWarning("Vertex input reflection exceeds max vertex attributes.");
				break;
			}

			const spirv_cross::SPIRType& type = compiler.get_type(input.type_id);
			VertexAttribute attribute{};
			attribute.location = static_cast<uint16_t>(compiler.get_decoration(input.id, spv::DecorationLocation));
			attribute.binding = 0;
			attribute.offset = 0;
			attribute.format = spirv_type_to_vertex_format(type);
			vertex_input.add_vertex_attribute(attribute);
		}
	}

	static void reflect_push_constants(spirv_cross::Compiler& compiler, const spirv_cross::SmallVector<spirv_cross::Resource>& push_constant_buffers,
		ParseResult& parse_result, AshShaderStageFlagBits stage)
	{
		if (push_constant_buffers.empty())
		{
			return;
		}

		parse_result.push_constants_count = 1;
		const spirv_cross::Resource& resource = push_constant_buffers.front();
		const spirv_cross::SPIRType& type = compiler.get_type(resource.base_type_id);
		parse_result.push_constants_stride = static_cast<uint32_t>(compiler.get_declared_struct_size(type));
		copy_name(parse_result.push_constant_name, sizeof(parse_result.push_constant_name), resource.name);

		uint32_t member_count = std::min<uint32_t>(static_cast<uint32_t>(type.member_types.size()), k_max_reflected_buffer_members);
		parse_result.push_constant_member_count = member_count;
		parse_result.pipeline_info.active_stages = static_cast<AshShaderStageFlagBits>(parse_result.pipeline_info.active_stages | stage);

		for (uint32_t i = 0; i < member_count; ++i)
		{
			const spirv_cross::SPIRType& member_type = compiler.get_type(type.member_types[i]);
			AshBufferMemberInfo& member = parse_result.push_constant_members[i];
			member.offset = compiler.type_struct_member_offset(type, i);
			member.size = static_cast<uint32_t>(compiler.get_declared_struct_member_size(type, i));
			member.type = spirv_type_to_shader_data_type(member_type);
			copy_name(member.name, sizeof(member.name), compiler.get_member_name(type.self, i));
			copy_name(member.fullName, sizeof(member.fullName), resource.name + "." + compiler.get_member_name(type.self, i));
		}
	}

	static void reflect_specialization_constants(spirv_cross::Compiler& compiler, ParseResult& parse_result)
	{
		const auto specialization_constants = compiler.get_specialization_constants();
		parse_result.specialization_constants_count = std::min<uint32_t>(static_cast<uint32_t>(specialization_constants.size()), k_max_specialization_constants);

		for (uint32_t i = 0; i < parse_result.specialization_constants_count; ++i)
		{
			const auto& constant = specialization_constants[i];
			const auto spirv_constant = compiler.get_constant(constant.id);
			const auto& spirv_type = compiler.get_type(spirv_constant.constant_type);
			SpecializationConstant& spec = parse_result.specialization_constants[i];
			SpecializationName& name = parse_result.specialization_names[i];

			spec.binding = static_cast<uint16_t>(constant.constant_id);
			spec.byte_stride = static_cast<uint16_t>(std::max(spirv_type.width / 8, 4u));

			switch (spirv_type.basetype)
			{
			case spirv_cross::SPIRType::Int:
				spec.default_value.type = ConstantValue::Type::Type_i32;
				spec.default_value.value.value_i = spirv_constant.scalar_i32();
				break;
			case spirv_cross::SPIRType::Float:
				spec.default_value.type = ConstantValue::Type::Type_f32;
				spec.default_value.value.value_f = spirv_constant.scalar_f32();
				break;
			case spirv_cross::SPIRType::UInt:
			default:
				spec.default_value.type = ConstantValue::Type::Type_u32;
				spec.default_value.value.value_u = spirv_constant.m.c[0].r[0].u32;
				break;
			}

			copy_name(name.name, sizeof(name.name), compiler.get_name(constant.id));
		}
	}

	bool parse_binary_spv(const uint32_t* data, size_t word_count, AshShaderStageFlagBits stage, ParseResult* parse_result)
	{
		ASH_SAFE_EXECUTE_BEGIN(bResult);
		ASH_PROCESS_ERROR(data);
		ASH_PROCESS_ERROR(word_count > 0);
		ASH_PROCESS_ERROR(parse_result);

		*parse_result = ParseResult{};

		spirv_cross::Compiler compiler(data, word_count);
		const spirv_cross::ShaderResources resources = compiler.get_shader_resources();

		if (stage == ASH_SHADER_STAGE_FLAG_BITS_MAX_ENUM)
		{
			const auto entry_points = compiler.get_entry_points_and_stages();
			if (!entry_points.empty())
			{
				stage = spirv_execution_model_to_ash(entry_points[0].execution_model);
			}
		}
		parse_result->pipeline_info.active_stages = stage;

		for (const auto& resource : resources.uniform_buffers)
		{
			reflect_descriptor_resource(compiler, *parse_result, resource, ASH_DESCRIPTOR_TYPE_UNIFORM_BUFFER, stage);
		}
		for (const auto& resource : resources.storage_buffers)
		{
			reflect_descriptor_resource(compiler, *parse_result, resource, ASH_DESCRIPTOR_TYPE_STORAGE_BUFFER, stage);
		}
		for (const auto& resource : resources.sampled_images)
		{
			reflect_descriptor_resource(compiler, *parse_result, resource, ASH_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, stage);
		}
		for (const auto& resource : resources.separate_images)
		{
			reflect_descriptor_resource(compiler, *parse_result, resource, ASH_DESCRIPTOR_TYPE_SAMPLED_IMAGE, stage);
		}
		for (const auto& resource : resources.separate_samplers)
		{
			reflect_descriptor_resource(compiler, *parse_result, resource, ASH_DESCRIPTOR_TYPE_SAMPLER, stage);
		}
		for (const auto& resource : resources.storage_images)
		{
			reflect_descriptor_resource(compiler, *parse_result, resource, ASH_DESCRIPTOR_TYPE_STORAGE_IMAGE, stage);
		}
		for (const auto& resource : resources.subpass_inputs)
		{
			reflect_descriptor_resource(compiler, *parse_result, resource, ASH_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, stage);
		}
		for (const auto& resource : resources.acceleration_structures)
		{
			reflect_descriptor_resource(compiler, *parse_result, resource, ASH_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE, stage);
		}

		if (stage == ASH_SHADER_STAGE_VERTEX_BIT)
		{
			reflect_vertex_inputs(compiler, resources.stage_inputs, *parse_result);
			finalize_vertex_input_layout(parse_result->pipeline_info.vertex_input);
		}

		if (stage == ASH_SHADER_STAGE_COMPUTE_BIT)
		{
			parse_result->pipeline_info.compute_local_size.x = compiler.get_execution_mode_argument(spv::ExecutionModeLocalSize, 0);
			parse_result->pipeline_info.compute_local_size.y = compiler.get_execution_mode_argument(spv::ExecutionModeLocalSize, 1);
			parse_result->pipeline_info.compute_local_size.z = compiler.get_execution_mode_argument(spv::ExecutionModeLocalSize, 2);
		}

		reflect_push_constants(compiler, resources.push_constant_buffers, *parse_result, stage);
		reflect_specialization_constants(compiler, *parse_result);

		ASH_SAFE_EXECUTE_END(bResult);
		return bResult;
	}

	bool merge_parse_results(const ParseResult* parse_results, uint32_t count, ParseResult* merged_result)
	{
		ASH_SAFE_EXECUTE_BEGIN(bResult);
		ASH_PROCESS_ERROR(parse_results);
		ASH_PROCESS_ERROR(merged_result);

		*merged_result = ParseResult{};

		for (uint32_t i = 0; i < count; ++i)
		{
			const ParseResult& src = parse_results[i];
			merged_result->pipeline_info.active_stages = static_cast<AshShaderStageFlagBits>(merged_result->pipeline_info.active_stages | src.pipeline_info.active_stages);

			for (uint32_t set_index = 0; set_index < src.set_count; ++set_index)
			{
				const DescriptorSetLayoutCreation& src_set = src.sets[set_index];
				DescriptorSetLayoutCreation* dst_set = find_or_add_set(*merged_result, src_set.set_index);
				dst_set->bindless = dst_set->bindless || src_set.bindless;
				dst_set->dynamic = dst_set->dynamic || src_set.dynamic;
				for (uint32_t binding_index = 0; binding_index < src_set.num_bindings; ++binding_index)
				{
					add_binding_if_unique(*dst_set, src_set.bindings[binding_index]);
				}
			}

			for (uint32_t attribute_index = 0; attribute_index < src.pipeline_info.vertex_input.num_vertex_attributes; ++attribute_index)
			{
				const VertexAttribute& src_attribute = src.pipeline_info.vertex_input.vertex_attributes[attribute_index];
				bool exists = false;
				for (uint32_t dst_index = 0; dst_index < merged_result->pipeline_info.vertex_input.num_vertex_attributes; ++dst_index)
				{
					if (merged_result->pipeline_info.vertex_input.vertex_attributes[dst_index].location == src_attribute.location)
					{
						exists = true;
						break;
					}
				}
				if (!exists)
				{
					merged_result->pipeline_info.vertex_input.add_vertex_attribute(src_attribute);
				}
			}

			if (src.push_constants_count > 0)
			{
				merged_result->push_constants_count = 1;
				merged_result->push_constants_stride = std::max(merged_result->push_constants_stride, src.push_constants_stride);
				if (merged_result->push_constant_name[0] == '\0' && src.push_constant_name[0] != '\0')
				{
					memcpy(merged_result->push_constant_name, src.push_constant_name, sizeof(merged_result->push_constant_name));
				}
				merged_result->push_constant_member_count = std::max(merged_result->push_constant_member_count, src.push_constant_member_count);
				for (uint32_t member_index = 0; member_index < src.push_constant_member_count; ++member_index)
				{
					merged_result->push_constant_members[member_index] = src.push_constant_members[member_index];
				}
			}

			for (uint32_t spec_index = 0; spec_index < src.specialization_constants_count; ++spec_index)
			{
				const SpecializationConstant& src_spec = src.specialization_constants[spec_index];
				const SpecializationName& src_name = src.specialization_names[spec_index];
				bool exists = false;
				for (uint32_t dst_index = 0; dst_index < merged_result->specialization_constants_count; ++dst_index)
				{
					if (strcmp(merged_result->specialization_names[dst_index].name, src_name.name) == 0)
					{
						exists = true;
						break;
					}
				}
				if (!exists && merged_result->specialization_constants_count < k_max_specialization_constants)
				{
					const uint32_t dst_index = merged_result->specialization_constants_count++;
					merged_result->specialization_constants[dst_index] = src_spec;
					merged_result->specialization_names[dst_index] = src_name;
				}
			}

			if (src.pipeline_info.compute_local_size.x != 1 || src.pipeline_info.compute_local_size.y != 1 || src.pipeline_info.compute_local_size.z != 1)
			{
				merged_result->pipeline_info.compute_local_size = src.pipeline_info.compute_local_size;
			}
		}

		finalize_vertex_input_layout(merged_result->pipeline_info.vertex_input);

		ASH_SAFE_EXECUTE_END(bResult);
		return bResult;
	}
}
