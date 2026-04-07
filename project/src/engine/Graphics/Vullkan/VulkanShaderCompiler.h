#pragma once
#include "Base/hplatform.h"
#include "Graphics/RHICommon.h"
#include "Graphics/ShaderCompiler.h"
#include "Graphics/DXC/DXCHelper.h"
#include "VulkanHelper.hpp"
#include "SpvHelper.h"
#include <memory>
using namespace AshEngine;
namespace RHI
{

	class DxcCompiler_VK 
	{
	public:
		bool init();
		void uninit();
		bool compile_shader_from_text(std::string const& pFullText, const ShaderItem& item, std::vector<uint32_t>& outSpirv, std::string& outErrorMsg);
	private:
		CComPtr<IDxcCompiler3> m_pCompiler = nullptr;
		CComPtr<IDxcUtils> m_pUtils = nullptr;
		std::unique_ptr<AshDXCContext> m_pDxcContext = nullptr;
	};

	class Shader;
	class VulkanShaderCompiler : public ShaderCompiler
	{
	public:
		VulkanShaderCompiler() = default;
		~VulkanShaderCompiler();
	public:
		bool check_and_compile_shader(ShaderItem const& fileInfo,  const std::string& shaderFullText, std::shared_ptr<Shader> pTargetShader) override;
		bool init() override;
		void uninit() override;
	private:
		std::unique_ptr<DxcCompiler_VK> m_pDxcCompiler = nullptr;
	};
}
