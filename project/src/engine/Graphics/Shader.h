#pragma once
#include "RHIResource.h"
#include "Base/hcore.h"
#include "Graphics/Pipeline.h"
#include <cstdint>
#include <string>
#include <vector>
namespace RHI
{
	enum class ShaderParameterValueType : uint8_t
	{
		Unknown = 0,
		Bool,
		Int,
		UInt,
		Float
	};

	enum class ShaderResourceBindingType : uint8_t
	{
		Unknown = 0,
		ConstantBuffer,
		ShaderResource,
		UnorderedAccess,
		Sampler,
		CombinedImageSampler
	};

	struct ShaderParameterMember
	{
		std::string name{};
		uint32_t offset = 0;
		uint32_t size = 0;
		uint32_t array_size = 1;
		ShaderParameterValueType value_type = ShaderParameterValueType::Unknown;
	};

	struct ShaderParameterBlockLayout
	{
		std::string name{};
		uint32_t bind_point = 0;
		uint32_t bind_space = 0;
		uint32_t byte_size = 0;
		std::vector<ShaderParameterMember> members{};
	};

	struct ShaderResourceBindingLayout
	{
		std::string name{};
		ShaderResourceBindingType type = ShaderResourceBindingType::Unknown;
		uint32_t bind_point = 0;
		uint32_t bind_space = 0;
		uint32_t bind_count = 1;
	};

	struct ShaderFile
	{

	};

	struct ShaderCreation
	{
		const char* pBaseShaderPath = nullptr;
		const char* pUserShaderPath = nullptr;
		const char* pGeneratedBindingsPath = nullptr;
		const char* pShaderDef = nullptr;
		const char* pShaderMacro = nullptr;
		const char* pEntryPoint = nullptr;
		AshShaderStageFlagBits	type = AshShaderStageFlagBits::ASH_SHADER_STAGE_FLAG_BITS_MAX_ENUM;
	};
	inline uint64_t get_shader_hash(const ShaderCreation& ci)
	{
		uint64_t hashCode = 0;
		ASH_HASH::hash_combine(hashCode, ci.pBaseShaderPath, ASH_HASH::CStringHash{});
		ASH_HASH::hash_combine(hashCode, ci.pUserShaderPath, ASH_HASH::CStringHash{});
		ASH_HASH::hash_combine(hashCode, ci.pGeneratedBindingsPath, ASH_HASH::CStringHash{});
		ASH_HASH::hash_combine(hashCode, ci.pShaderDef, ASH_HASH::CStringHash{});
		ASH_HASH::hash_combine(hashCode, ci.pShaderMacro, ASH_HASH::CStringHash{});
		ASH_HASH::hash_combine(hashCode, ci.pEntryPoint, ASH_HASH::CStringHash{});
		ASH_HASH::hash_combine(hashCode, ci.type);
		return hashCode;
	}
	struct ShaderCode
	{
		uint32_t size = 0;
		std::vector<uint32_t> codeData;
	};
	class Shader : public RHIResource
	{
	public:
		Shader() = default;
		virtual ~Shader() = default;
	public:
		static auto load_from_file(const ShaderCreation& ci) -> ShaderCode;
		virtual const std::vector<ShaderParameterBlockLayout>& get_parameter_block_layouts() const
		{
			static const std::vector<ShaderParameterBlockLayout> empty_layouts{};
			return empty_layouts;
		}
		virtual const std::vector<ShaderResourceBindingLayout>& get_resource_binding_layouts() const
		{
			static const std::vector<ShaderResourceBindingLayout> empty_layouts{};
			return empty_layouts;
		}
		virtual bool get_reflected_vertex_inputs(VertexInputCreation& out_vertex_input) const
		{
			out_vertex_input = VertexInputCreation{};
			return false;
		}
	};

}
