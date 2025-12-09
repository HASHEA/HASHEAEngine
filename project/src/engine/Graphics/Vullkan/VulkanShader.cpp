#include "VulkanShader.h"
#include "VulkanPipeline.h"
#include "VulkanDescriptorSet.h"
#include "Base/hmemory.h"
#include "Base/hcache.h"
#include "Base/hfile.h"
#include "Base/hstring.h"
namespace RHI
{
	auto VulkanShader::get_native_handle() -> void*
	{
		return nullptr;
	}

	auto VulkanShader::get_name() -> const char*
	{
		return nullptr;
	}
	VulkanShader::VulkanShader(const ShaderCreation& ci)
	{
		ShaderCode _code = Shader::load_from_file(ci);

	}
	VulkanShader::~VulkanShader()
	{
	}
}