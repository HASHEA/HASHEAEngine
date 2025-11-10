#pragma once
#include "RHIResource.h"
#include <vector>
namespace RHI
{

	struct ShaderCreation
	{
		const char* pBaseShaderPath = nullptr;
		const char* pUserShaderPath = nullptr;
		const char* pShaderDef = nullptr;
		const char* pShaderMacro = nullptr;
		AshShaderStageFlagBits	type = AshShaderStageFlagBits::ASH_SHADER_STAGE_FLAG_BITS_MAX_ENUM;
	};
	class Shader : public RHIResource
	{
	public:
		Shader() = default;
		virtual ~Shader() {};

	};

}