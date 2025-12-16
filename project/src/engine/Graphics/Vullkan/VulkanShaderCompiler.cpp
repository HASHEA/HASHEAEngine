#include "VulkanShader.h"
#include "VulkanShaderCompiler.h"
#include "VulkanPipeline.h"
#include "VulkanDescriptorSet.h"
#include "Graphics/ShaderCache.h"
#include "Graphics/DXC/DXCHelper.h"
//
#include "Base/hmemory.h"
#include "Base/hcache.h"
#include "Base/hfile.h"
#include "Base/hstring.h"
#include <string>
namespace RHI
{
	VulkanShaderCompiler::~VulkanShaderCompiler()
	{
		uninit();
	}
	bool VulkanShaderCompiler::check_and_compile_shader(ShaderItem const& fileInfo, const std::string& shaderFullText, std::shared_ptr<Shader> pTargetShader)
	{
		ASH_SAFE_EXECUTE_BEGIN(bResult);
		DigestBuilder<SHA1>             passkeybuilder{};
		DigestBuilder<SHA1>             shaderTextbuilder{};
		CComPtr<IDxcBlobEncoding>       pBlobEncoding = nullptr;
		ASH_SAFE_EXECUTE_END(bResult);
		return bResult;
	}
	bool VulkanShaderCompiler::init()
	{
		return false;
	}
	void VulkanShaderCompiler::uninit()
	{
	}
	/************* dxc compiler for vulkan ****************/
	bool DxcCompiler_VK::init()
	{
		return false;
	}
	void DxcCompiler_VK::uninit()
	{
	}

	bool DxcCompiler_VK::compile_shader_from_text(std::string const& pFullText)
	{
		return false;
	}
}