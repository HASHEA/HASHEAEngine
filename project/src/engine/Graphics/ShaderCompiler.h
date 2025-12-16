#pragma once
#include "RHIResource.h"
#include "Base/hcore.h"
#include <vector>
namespace RHI
{
	struct ShaderItem;
	class ShaderCompiler
	{
	public:
		ShaderCompiler() = default;
		virtual ~ShaderCompiler() {};
	public:
		virtual bool init() = 0;
		virtual void uninit() = 0;
		virtual bool check_and_compile_shader(ShaderItem const& shaderInfo, const std::string& shaderFullText, std::shared_ptr<Shader> pTargetShader) = 0;
	};
}