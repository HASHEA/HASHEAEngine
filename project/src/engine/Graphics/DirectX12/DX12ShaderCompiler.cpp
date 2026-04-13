#include "DX12ShaderCompiler.h"
#include "DX12Shader.h"
#include "Base/hlog.h"

namespace RHI
{
	bool DX12ShaderCompiler::init()
	{
		HRESULT hr = DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&m_compiler));
		if (FAILED(hr))
		{
			HLogError("DX12ShaderCompiler: Failed to create DXC compiler.");
			return false;
		}

		hr = DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&m_utils));
		if (FAILED(hr))
		{
			HLogError("DX12ShaderCompiler: Failed to create DXC utils.");
			return false;
		}

		return true;
	}

	void DX12ShaderCompiler::uninit()
	{
		m_compiler.Reset();
		m_utils.Reset();
	}

	bool DX12ShaderCompiler::check_and_compile_shader(ShaderItem const& shaderInfo, const std::string& shaderFullText, std::shared_ptr<Shader> pTargetShader)
	{
		// DX12 shader compilation is handled in DX12Shader::init() directly
		// This method is called from the shader hot-reload system
		auto* dx12Shader = static_cast<DX12Shader*>(pTargetShader.get());
		// Re-initialization would be done through create_shader in the context
		return true;
	}
}
