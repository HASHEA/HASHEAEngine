#pragma once
#include "Graphics/Shader.h"
#include "Graphics/DescriptorSetLayout.h"
#include "VulkanWrapper.h"
#include "SpvHelper.h"
#include <vector>

namespace RHI
{
	class VulkanShader : public Shader
	{
	public:
		VulkanShader() = default;
		~VulkanShader() override;

		bool init(const ShaderCreation& ci);
		void set_compiled_binary(const ShaderCreation& ci, const std::vector<uint32_t>& spirv_binary, const ParseResult& reflection_data);

		const std::vector<uint32_t>& get_spirv_binary() const;
		const ParseResult& get_reflection_data() const;
		const VkPipelineShaderStageCreateInfo& get_stage_info() const;
		AshShaderStageFlagBits get_stage() const;
		uint32_t get_descriptor_set_layout_count() const;
		std::shared_ptr<DescriptorSetLayout> get_descriptor_set_layout(uint32_t index) const;
		const VertexInputCreation& get_vertex_input() const;
		const std::vector<ShaderParameterBlockLayout>& get_parameter_block_layouts() const override;
		const std::vector<ShaderResourceBindingLayout>& get_resource_binding_layouts() const override;
		bool get_reflected_vertex_inputs(VertexInputCreation& out_vertex_input) const override;

		auto get_native_handle() -> void* override;
		auto get_name() -> const char* override;

	private:
		ShaderCreation m_creation{};
		std::vector<uint32_t> m_spirv_binary;
		ParseResult m_reflection_data{};
		std::vector<ShaderParameterBlockLayout> m_parameter_block_layouts;
		std::vector<ShaderResourceBindingLayout> m_resource_binding_layouts;
		std::vector<std::shared_ptr<DescriptorSetLayout>> m_descriptor_set_layouts;
		VkShaderModule m_shader_module = VK_NULL_HANDLE;
		VkPipelineShaderStageCreateInfo m_shader_stage_info{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
		char m_name[64]{};
	};
}
