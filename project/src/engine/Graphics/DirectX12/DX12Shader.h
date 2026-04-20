#pragma once
#include "DX12Wrapper.h"
#include "Graphics/Shader.h"
#include <vector>
#include <string>
#include <unordered_map>

struct ID3D12ShaderReflection;

namespace RHI
{
	// Reflection data from ID3D12ShaderReflection
	struct DX12ShaderBindingInfo
	{
		std::string name;
		uint32_t bindPoint = 0;     // register index
		uint32_t bindSpace = 0;     // register space
		uint32_t bindCount = 1;
		uint32_t byteSize = 0;
		D3D_SHADER_INPUT_TYPE type = D3D_SIT_CBUFFER;
		D3D_SRV_DIMENSION dimension = D3D_SRV_DIMENSION_UNKNOWN;
	};

	struct DX12ShaderVertexInput
	{
		std::string semanticName;
		uint32_t semanticIndex = 0;
		uint32_t registerIndex = 0;
		AshVertexComponentFormat format = AshVertexComponentFormat::FormatCount;
	};

	struct DX12ShaderReflectionData
	{
		std::vector<DX12ShaderBindingInfo> cbuffers;
		std::vector<DX12ShaderBindingInfo> srvs;
		std::vector<DX12ShaderBindingInfo> uavs;
		std::vector<DX12ShaderBindingInfo> samplers;
		std::vector<DX12ShaderVertexInput> vertexInputs;
		uint32_t threadGroupSizeX = 1;
		uint32_t threadGroupSizeY = 1;
		uint32_t threadGroupSizeZ = 1;
	};

	class DX12Shader : public Shader
	{
	public:
		DX12Shader() = default;
		~DX12Shader() = default;

		bool init(const ShaderCreation& ci);

		const std::vector<uint8_t>& get_bytecode() const { return m_bytecode; }
		D3D12_SHADER_BYTECODE get_d3d12_bytecode() const
		{
			return { m_bytecode.data(), m_bytecode.size() };
		}
		const DX12ShaderReflectionData& get_reflection_data() const { return m_reflectionData; }
		AshShaderStageFlagBits get_stage() const { return m_stage; }
		const std::vector<ShaderParameterBlockLayout>& get_parameter_block_layouts() const override { return m_parameterBlockLayouts; }
		bool get_reflected_vertex_inputs(VertexInputCreation& out_vertex_input) const override;

	public:
		auto get_native_handle() -> void* override { return nullptr; }
		auto get_name() -> const char* override { return m_name.c_str(); }

	private:
		bool _compile(const ShaderCreation& ci);
		void _reflect_from(ID3D12ShaderReflection* reflection);

	private:
		std::vector<uint8_t> m_bytecode;
		DX12ShaderReflectionData m_reflectionData;
		std::vector<ShaderParameterBlockLayout> m_parameterBlockLayouts;
		AshShaderStageFlagBits m_stage = ASH_SHADER_STAGE_FLAG_BITS_MAX_ENUM;
		std::string m_name;
	};
}
