//#include "AshSpvParser.h"
//namespace AshEngine
//{
//	inline auto spv_type_to_ash(const spirv_cross::SPIRType& InputType) -> RHI::AshFormat
//	{
//		RHI::AshFormat uintTypes[] =
//		{
//			RHI::ASH_FORMAT_R32_UINT,
//			RHI::ASH_FORMAT_R32G32_UINT,
//			RHI::ASH_FORMAT_R32G32B32_UINT,
//			RHI::ASH_FORMAT_R32G32B32A32_UINT };
//
//		RHI::AshFormat intTypes[] =
//		{
//			RHI::ASH_FORMAT_R32_SINT,
//			RHI::ASH_FORMAT_R32G32_SINT,
//			RHI::ASH_FORMAT_R32G32B32_SINT,
//			RHI::ASH_FORMAT_R32G32B32A32_SINT };
//
//		RHI::AshFormat floatTypes[] =
//		{
//			RHI::ASH_FORMAT_R32_SFLOAT,
//			RHI::ASH_FORMAT_R32G32_SFLOAT,
//			RHI::ASH_FORMAT_R32G32B32_SFLOAT,
//			RHI::ASH_FORMAT_R32G32B32A32_SFLOAT };
//
//		RHI::AshFormat doubleTypes[] =
//		{
//			RHI::ASH_FORMAT_R64_SFLOAT,
//			RHI::ASH_FORMAT_R64G64_SFLOAT,
//			RHI::ASH_FORMAT_R64G64B64_SFLOAT,
//			RHI::ASH_FORMAT_R64G64B64A64_SFLOAT,
//		};
//		switch (InputType.basetype)
//		{
//		case spirv_cross::SPIRType::UInt:
//			return uintTypes[InputType.vecsize - 1];
//		case spirv_cross::SPIRType::Int:
//			return intTypes[InputType.vecsize - 1];
//		case spirv_cross::SPIRType::Float:
//			return floatTypes[InputType.vecsize - 1];
//		case spirv_cross::SPIRType::Double:
//			return doubleTypes[InputType.vecsize - 1];
//		default:
//			HLogWarning("Cannot find VK_Format : {0}", InputType.basetype);
//			return RHI::ASH_FORMAT_R32G32B32A32_SFLOAT;
//		}
//	};
//
//	inline auto spv_data_type_to_ash_data_type(const spirv_cross::SPIRType& InputType) -> RHI::AshShaderDataType
//	{
//		switch (InputType.basetype)
//		{
//		case spirv_cross::SPIRType::Boolean:
//			return RHI::AshShaderDataType::ASH_Bool;
//		case spirv_cross::SPIRType::Int:
//			if (InputType.vecsize == 1)
//				return RHI::AshShaderDataType::ASH_Int;
//			if (InputType.vecsize == 2)
//				return RHI::AshShaderDataType::ASH_IVec2;
//			if (InputType.vecsize == 3)
//				return RHI::AshShaderDataType::ASH_IVec3;
//			if (InputType.vecsize == 4)
//				return RHI::AshShaderDataType::ASH_IVec4;
//		case spirv_cross::SPIRType::UInt:
//			return RHI::AshShaderDataType::ASH_UInt;
//		case spirv_cross::SPIRType::Float:
//			if (InputType.columns == 3)
//				return RHI::AshShaderDataType::ASH_Mat3;
//			if (InputType.columns == 4)
//				return RHI::AshShaderDataType::ASH_Mat4;
//
//			if (InputType.vecsize == 1)
//				return RHI::AshShaderDataType::ASH_Float32;
//			if (InputType.vecsize == 2)
//				return RHI::AshShaderDataType::ASH_Vec2;
//			if (InputType.vecsize == 3)
//				return RHI::AshShaderDataType::ASH_Vec3;
//			if (InputType.vecsize == 4)
//				return RHI::AshShaderDataType::ASH_Vec4;
//			break;
//		case spirv_cross::SPIRType::Struct:
//			return RHI::AshShaderDataType::ASH_Struct;
//		}
//		HLogWarning("Unknown spirv type!");
//		return RHI::AshShaderDataType::ASH_None;
//	};
//
//
//	//we certainly can get out the member/detial info of each resource, but we don't need them for creating layouts
//	void parse_binary_spv_cross(const uint32_t* data, size_t data_size, StringBuffer& name_buffer, ParseResult* parse_result)
//	{
//		H_ASSERT(parse_result);
//		spirv_cross::Compiler comp(data, data_size);
//		spirv_cross::ShaderResources resources = comp.get_shader_resources();
//		//process on inputs only for vertex shader
//		uint32_t vertexInputStride = 0;
//		for (const spirv_cross::Resource& resource : resources.stage_inputs)
//		{
//			const spirv_cross::SPIRType& InputType = comp.get_type(resource.type_id);
//			RHI::AshInputAttributeDescription description = {};
//			description.binding = comp.get_decoration(resource.id, spv::DecorationBinding);
//			description.location = comp.get_decoration(resource.id, spv::DecorationLocation);
//			description.offset = vertexInputStride;
//			description.format = spv_type_to_ash(InputType);
//			parse_result->inputs[parse_result->intputs_count] = description;
//			vertexInputStride += get_stride_from_ash_format(description.format);
//			parse_result->intputs_count++;
//		}
//		//process on uniform buffers
//		for (auto& u : resources.uniform_buffers)
//		{
//			uint32_t set = comp.get_decoration(u.id, spv::DecorationDescriptorSet);
//			uint32_t binding = comp.get_decoration(u.id, spv::DecorationBinding);
//			auto& type = comp.get_type(u.type_id);
//			HLogInfo("parssing binary : uniform {0} at set = {1}, binding = {2}", u.name, set, binding);
//			auto& DescriptorSetInfo = parse_result->sets[set];
//			DescriptorSetInfo.add_binding(RHI::AshDescriptorType::ASH_DESCRIPTOR_TYPE_UNIFORM_BUFFER, binding,1, u.name.c_str());
//		}
//		//process on pushconstant
//		for (auto& u : resources.push_constant_buffers)
//		{
//			uint32_t set = comp.get_decoration(u.id, spv::DecorationDescriptorSet);
//			uint32_t binding = comp.get_decoration(u.id, spv::DecorationBinding);
//			uint32_t binding3 = comp.get_decoration(u.id, spv::DecorationOffset);
//			auto& type = comp.get_type(u.type_id);
//			auto ranges = comp.get_active_buffer_ranges(u.id);
//			uint32_t size = 0;
//			for (auto& range : ranges)
//			{
//				HLogInfo("\tAccessing Member {0} offset {1}, size {2}", range.index, range.offset, range.range);
//				size += uint32_t(range.range);
//			}
//			HLogInfo("Push Constant {0} at set = {1}, binding = {2}", u.name.c_str(), set, binding);
//			auto& push = parse_result->pushConsts[parse_result->push_constants_count];
//			push.name = u.name;
//			push.size = size;
//			auto& bufferType = comp.get_type(u.base_type_id);
//			auto    bufferSize = comp.get_declared_struct_size(bufferType);
//			int32_t memberCount = (int32_t)bufferType.member_types.size();
//			H_ASSERTLOG(memberCount <= k_max_count,"the member count in the push constant : {0} is {1}, exceed the max count : {2}", push.name,memberCount,k_max_count);
//			for (int32_t i = 0; i < memberCount; i++)
//			{
//				auto        type = comp.get_type(bufferType.member_types[i]);
//				const auto& memberName = comp.get_member_name(bufferType.self, i);
//				auto        size = comp.get_declared_struct_member_size(bufferType, i);
//				auto        offset = comp.type_struct_member_offset(bufferType, i);
//
//				std::string uniformName = u.name + "." + memberName;
//
//				auto& member = push.memberInfos[i];
//				member.size = (uint32_t)size;
//				member.offset = offset;
//				member.type = spv_data_type_to_ash_data_type(type);
//				member.fullName = uniformName;
//				member.name = memberName;
//			}
//			push.memberCount = memberCount;
//			parse_result->push_constants_count++;
//		}
//		//process on sampled images
//		for (auto& u : resources.sampled_images)
//		{
//			uint32_t set = comp.get_decoration(u.id, spv::DecorationDescriptorSet);
//			uint32_t binding = comp.get_decoration(u.id, spv::DecorationBinding);
//			auto& type = comp.get_type(u.type_id);
//			HLogInfo("parssing binary : uniform {0} at set = {1}, binding = {2}", u.name, set, binding);
//			auto& DescriptorSetInfo = parse_result->sets[set];
//
//			bool is_runtime_array = (!type.array.empty()) &&
//				(type.array.back() == 0) &&
//				(type.storage == spv::StorageClassUniformConstant);
//			bool is_non_uniform = comp.has_decoration(u.id, spv::DecorationNonUniform);
//			if (is_runtime_array && is_non_uniform)
//			{
//				DescriptorSetInfo.add_binding_bindless(RHI::AshDescriptorType::ASH_DESCRIPTOR_TYPE_SAMPLED_IMAGE, binding, u.name.c_str());
//			}
//			else
//			{
//				DescriptorSetInfo.add_binding(RHI::AshDescriptorType::ASH_DESCRIPTOR_TYPE_SAMPLED_IMAGE, binding, 1, u.name.c_str());
//			}
//			
//		}
//		//process on storage images
//		for (auto& u : resources.storage_images)
//		{
//			uint32_t set = comp.get_decoration(u.id, spv::DecorationDescriptorSet);
//			uint32_t binding = comp.get_decoration(u.id, spv::DecorationBinding);
//			auto& type = comp.get_type(u.type_id);
//			HLogInfo("parssing binary : uniform {0} at set = {1}, binding = {2}", u.name, set, binding);
//			auto& DescriptorSetInfo = parse_result->sets[set];
//			DescriptorSetInfo.add_binding(RHI::AshDescriptorType::ASH_DESCRIPTOR_TYPE_SAMPLED_IMAGE, binding, 1, u.name.c_str());
//		}
//	}
//}
//
