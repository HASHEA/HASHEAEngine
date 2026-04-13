#include "VulkanShader.h"
#include "VulkanContext.h"
#include "VulkanDescriptorSet.h"
#include "Base/hlog.h"
#include <cstring>

namespace RHI
{
	namespace
	{
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

	auto VulkanShader::get_native_handle() -> void*
	{
		return m_shader_module;
	}

	auto VulkanShader::get_name() -> const char*
	{
		return m_name;
	}
}
