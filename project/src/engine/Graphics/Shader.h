#pragma once
#include "RHIResource.h"
#include <vector>
namespace RHI
{
	

	struct ShaderCreation
	{
		std::vector<uint32_t> binCode;
		size_t codeSize = 0;
		AshShaderStageFlagBits	type = AshShaderStageFlagBits::ASH_SHADER_STAGE_FLAG_BITS_MAX_ENUM;
	};
	class Shader : public RHIResource
	{
	public:
		Shader() = default;
		virtual ~Shader() {};

	};

}