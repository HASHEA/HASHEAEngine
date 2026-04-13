#pragma once
#include "Graphics/ShaderCompiler.h"
#include "DX12Wrapper.h"

namespace RHI
{
	class DX12ShaderCompiler : public ShaderCompiler
	{
	public:
		DX12ShaderCompiler() = default;
		~DX12ShaderCompiler() = default;

		bool init() override;
		void uninit() override;
		bool check_and_compile_shader(ShaderItem const& shaderInfo, const std::string& shaderFullText, std::shared_ptr<Shader> pTargetShader) override;

	private:
		ComPtr<IDxcCompiler3> m_compiler;
		ComPtr<IDxcUtils> m_utils;
	};
}
