#include "VulkanShader.h"
#include "VulkanContext.h"
#include "VulkanDescriptorSet.h"
#include "Base/hlog.h"
#include "spirv.hpp"
#include "spirv_cross.hpp"
#include <algorithm>
#include <cstring>

namespace RHI
{
	namespace
	{
		static AshVertexComponentFormat spirv_type_to_vertex_component_format(const spirv_cross::SPIRType& type)
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

		static ShaderParameterValueType ash_shader_data_type_to_parameter_type(AshShaderDataType type)
		{
			switch (type)
			{
			case ASH_Bool:
				return ShaderParameterValueType::Bool;
			case ASH_Int32:
			case ASH_IVec2:
			case ASH_IVec3:
			case ASH_IVec4:
				return ShaderParameterValueType::Int;
			case ASH_UInt:
				return ShaderParameterValueType::UInt;
			case ASH_Float32:
			case ASH_Vec2:
			case ASH_Vec3:
			case ASH_Vec4:
			case ASH_Mat4:
				return ShaderParameterValueType::Float;
			default:
				return ShaderParameterValueType::Unknown;
			}
		}

		static void build_parameter_block_layouts_from_reflection(
			const ParseResult& reflection_data,
			std::vector<ShaderParameterBlockLayout>& out_layouts)
		{
			out_layouts.clear();
			if (reflection_data.push_constants_count == 0 || reflection_data.push_constants_stride == 0)
			{
				return;
			}

			ShaderParameterBlockLayout layout{};
			layout.name = reflection_data.push_constant_name[0] != '\0' ? reflection_data.push_constant_name : "AshRootConstants";
			layout.byte_size = reflection_data.push_constants_stride;

			for (uint32_t member_index = 0; member_index < reflection_data.push_constant_member_count; ++member_index)
			{
				const AshBufferMemberInfo& src_member = reflection_data.push_constant_members[member_index];
				ShaderParameterMember dst_member{};
				dst_member.name = src_member.name;
				dst_member.offset = src_member.offset;
				dst_member.size = src_member.size;
				dst_member.array_size = 1;
				dst_member.value_type = ash_shader_data_type_to_parameter_type(src_member.type);
				layout.members.push_back(std::move(dst_member));
			}

			out_layouts.push_back(std::move(layout));
		}

		static ShaderResourceBindingType ash_descriptor_type_to_shader_resource_binding_type(AshDescriptorType descriptor_type)
		{
			switch (descriptor_type)
			{
			case ASH_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
			case ASH_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
				return ShaderResourceBindingType::ConstantBuffer;
			case ASH_DESCRIPTOR_TYPE_STORAGE_BUFFER:
			case ASH_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
			case ASH_DESCRIPTOR_TYPE_STORAGE_IMAGE:
			case ASH_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
				return ShaderResourceBindingType::UnorderedAccess;
			case ASH_DESCRIPTOR_TYPE_SAMPLER:
				return ShaderResourceBindingType::Sampler;
			case ASH_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
				return ShaderResourceBindingType::CombinedImageSampler;
			case ASH_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
			case ASH_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
			case ASH_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
				return ShaderResourceBindingType::ShaderResource;
			default:
				return ShaderResourceBindingType::Unknown;
			}
		}

		static void build_resource_binding_layouts_from_reflection(
			const ParseResult& reflection_data,
			std::vector<ShaderResourceBindingLayout>& out_layouts)
		{
			out_layouts.clear();
			for (uint32_t set_index = 0; set_index < reflection_data.set_count; ++set_index)
			{
				const DescriptorSetLayoutCreation& descriptor_set = reflection_data.sets[set_index];
				for (uint32_t binding_index = 0; binding_index < descriptor_set.num_bindings; ++binding_index)
				{
					const DescriptorSetLayoutCreation::Binding& binding = descriptor_set.bindings[binding_index];
					ShaderResourceBindingLayout layout{};
					layout.name = binding.name;
					layout.type = ash_descriptor_type_to_shader_resource_binding_type(binding.type);
					layout.bind_point = binding.index;
					layout.bind_space = descriptor_set.set_index;
					layout.bind_count = std::max<uint32_t>(binding.count, 1u);
					out_layouts.push_back(std::move(layout));
				}
			}

			std::sort(
				out_layouts.begin(),
				out_layouts.end(),
				[](const ShaderResourceBindingLayout& lhs, const ShaderResourceBindingLayout& rhs)
				{
					if (lhs.bind_space != rhs.bind_space)
					{
						return lhs.bind_space < rhs.bind_space;
					}
					if (lhs.bind_point != rhs.bind_point)
					{
						return lhs.bind_point < rhs.bind_point;
					}
					if (lhs.type != rhs.type)
					{
						return static_cast<uint8_t>(lhs.type) < static_cast<uint8_t>(rhs.type);
					}
					return lhs.name < rhs.name;
				});
		}
	}

	VulkanShader::~VulkanShader()
	{
		if (m_shader_module != VK_NULL_HANDLE)
		{
			vkDestroyShaderModule(VulkanContext::get_vulkan_device(), m_shader_module, VulkanContext::get_vulkan_allocation_callbacks());
			m_shader_module = VK_NULL_HANDLE;
		}
	}

	bool VulkanShader::init(const ShaderCreation& ci)
	{
		m_creation = ci;
		const char* shader_name = ci.pBaseShaderPath ? ci.pBaseShaderPath : "VulkanShader";
		const size_t max_copy = sizeof(m_name) - 1;
		const size_t name_length = std::min(strlen(shader_name), max_copy);
		memcpy(m_name, shader_name, name_length);
		m_name[name_length] = '\0';
		m_reflection_data = ParseResult{};
		m_spirv_binary.clear();
		m_parameter_block_layouts.clear();
		m_resource_binding_layouts.clear();
		m_descriptor_set_layouts.clear();
		m_shader_stage_info = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
		m_shader_stage_info.pName = ci.pEntryPoint ? ci.pEntryPoint : "main";
		m_shader_stage_info.stage = ash_shader_stage_to_vk(ci.type);
		return true;
	}

	void VulkanShader::set_compiled_binary(const ShaderCreation& ci, const std::vector<uint32_t>& spirv_binary, const ParseResult& reflection_data)
	{
		m_creation = ci;
		m_spirv_binary = spirv_binary;
		m_reflection_data = reflection_data;
		build_parameter_block_layouts_from_reflection(m_reflection_data, m_parameter_block_layouts);
		build_resource_binding_layouts_from_reflection(m_reflection_data, m_resource_binding_layouts);
		m_descriptor_set_layouts.clear();
		H_ASSERTLOG(!m_spirv_binary.empty(), "Compiled SPIR-V is empty for shader {}", ci.pBaseShaderPath ? ci.pBaseShaderPath : "<null>");

		if (m_shader_module != VK_NULL_HANDLE)
		{
			vkDestroyShaderModule(VulkanContext::get_vulkan_device(), m_shader_module, VulkanContext::get_vulkan_allocation_callbacks());
			m_shader_module = VK_NULL_HANDLE;
		}

		VkShaderModuleCreateInfo create_info{ VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
		create_info.codeSize = m_spirv_binary.size() * sizeof(uint32_t);
		create_info.pCode = m_spirv_binary.data();
		VK_CHECK_RESULT(vkCreateShaderModule(VulkanContext::get_vulkan_device(), &create_info, VulkanContext::get_vulkan_allocation_callbacks(), &m_shader_module));
		VulkanContext::set_resource_name(VK_OBJECT_TYPE_SHADER_MODULE, (uint64_t)m_shader_module, get_name());

		m_shader_stage_info = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
		m_shader_stage_info.stage = ash_shader_stage_to_vk(ci.type);
		m_shader_stage_info.module = m_shader_module;
		m_shader_stage_info.pName = ci.pEntryPoint ? ci.pEntryPoint : "main";

		for (uint32_t i = 0; i < m_reflection_data.set_count; ++i)
		{
			auto layout = VulkanDescriptorSetLayout::create(m_reflection_data.sets[i]);
			H_ASSERT(layout);
			m_descriptor_set_layouts.push_back(layout);
		}
	}

	const std::vector<uint32_t>& VulkanShader::get_spirv_binary() const
	{
		return m_spirv_binary;
	}

	const ParseResult& VulkanShader::get_reflection_data() const
	{
		return m_reflection_data;
	}

	const VkPipelineShaderStageCreateInfo& VulkanShader::get_stage_info() const
	{
		return m_shader_stage_info;
	}

	AshShaderStageFlagBits VulkanShader::get_stage() const
	{
		return m_creation.type;
	}

	uint32_t VulkanShader::get_descriptor_set_layout_count() const
	{
		return static_cast<uint32_t>(m_descriptor_set_layouts.size());
	}

	std::shared_ptr<DescriptorSetLayout> VulkanShader::get_descriptor_set_layout(uint32_t index) const
	{
		H_ASSERT(index < m_descriptor_set_layouts.size());
		return m_descriptor_set_layouts[index];
	}

	const VertexInputCreation& VulkanShader::get_vertex_input() const
	{
		return m_reflection_data.pipeline_info.vertex_input;
	}

	const std::vector<ShaderParameterBlockLayout>& VulkanShader::get_parameter_block_layouts() const
	{
		return m_parameter_block_layouts;
	}

	const std::vector<ShaderResourceBindingLayout>& VulkanShader::get_resource_binding_layouts() const
	{
		return m_resource_binding_layouts;
	}

	bool VulkanShader::get_reflected_vertex_inputs(VertexInputCreation& out_vertex_input) const
	{
		out_vertex_input = VertexInputCreation{};
		if (m_creation.type != ASH_SHADER_STAGE_VERTEX_BIT || m_spirv_binary.empty())
		{
			return false;
		}

		spirv_cross::Compiler compiler(m_spirv_binary);
		const auto active_interface_variables = compiler.get_active_interface_variables();
		const spirv_cross::ShaderResources resources = compiler.get_shader_resources(active_interface_variables);
		for (const spirv_cross::Resource& input : resources.stage_inputs)
		{
			if (out_vertex_input.num_vertex_attributes >= k_max_vertex_attributes)
			{
				HLogWarning("VulkanShader '{}' reflected more active vertex inputs than the shared RHI limit allows.", m_name);
				break;
			}

			const spirv_cross::SPIRType& input_type = compiler.get_type(input.type_id);
			VertexAttribute attribute{};
			attribute.location = static_cast<uint16_t>(compiler.get_decoration(input.id, spv::DecorationLocation));
			attribute.binding = 0;
			attribute.offset = 0;
			attribute.format = spirv_type_to_vertex_component_format(input_type);
			out_vertex_input.add_vertex_attribute(attribute);
		}

		std::sort(
			out_vertex_input.vertex_attributes,
			out_vertex_input.vertex_attributes + out_vertex_input.num_vertex_attributes,
			[](const VertexAttribute& lhs, const VertexAttribute& rhs)
			{
				return lhs.location < rhs.location;
			});

		return out_vertex_input.num_vertex_attributes > 0;
	}

	auto VulkanShader::get_native_handle() -> void*
	{
		return m_shader_module;
	}

	auto VulkanShader::get_name() -> const char*
	{
		return m_name;
	}
}
