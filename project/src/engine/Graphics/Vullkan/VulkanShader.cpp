#include "VulkanShader.h"
#include "VulkanPipeline.h"
#include "VulkanDescriptorSet.h"
#include "Base/hmemory.h"
namespace RHI
{
	VulkanShader::VulkanShader(const ShaderStateCreation& ci)
	{
	}
	VulkanShader::~VulkanShader()
	{
	}
	auto VulkanShader::create(const ShaderStateCreation& ci) -> std::shared_ptr<VulkanShader>
	{
		return Ash_New_Shared<VulkanShader>(ci);
	}
}