#pragma once
#include "RHIResource.h"
#include "Base/hcore.h"
#include <vector>
namespace RHI
{

	struct ShaderFile
	{

	};

	struct ShaderCreation
	{
		const char* pBaseShaderPath = nullptr;
		const char* pUserShaderPath = nullptr;
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
		virtual ~Shader() {};
	public:
		static auto load_from_file(const ShaderCreation& ci) -> ShaderCode;
	};

}
