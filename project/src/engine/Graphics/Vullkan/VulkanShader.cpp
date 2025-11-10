#include "VulkanShader.h"
#include "VulkanPipeline.h"
#include "VulkanDescriptorSet.h"
#include "Base/hmemory.h"
#include "Base/hcachedefine.h"
#include "Base/hfile.h"
#include "Base/hstring.h"
namespace RHI
{
	auto VulkanShader::create(const ShaderCreation& ci) -> std::shared_ptr<VulkanShader>
	{
		return Ash_New_Shared<VulkanShader>(ci);
	}

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

	}
	VulkanShader::~VulkanShader()
	{
	}
}