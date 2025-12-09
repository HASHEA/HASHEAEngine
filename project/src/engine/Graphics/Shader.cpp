#include "Shader.h"
#include "Base/hfile.h"
namespace RHI
{
	auto Shader::load_from_file(const ShaderCreation& ci) -> ShaderCode
	{
		std::vector<char> shaderFullText;
		if (ci.pBaseShaderPath)
		{
			auto pBaseShaderContent = AshEngine::file_read_text(ci.pBaseShaderPath);
		}
		if (ci.pUserShaderPath)
		{
			auto pUserShaderContent = AshEngine::file_read_text(ci.pUserShaderPath);
		}

		return ShaderCode();
	}
};