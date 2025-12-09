#include "VulkanRenderProgram.h"
#include "VulkanHelper.hpp"
#include "VulkanContext.h"
#include "VulkanShader.h"
namespace RHI
{
	static IRenderProgramBinder default{};
	bool VulkanGraphicsRenderProgram::create(std::shared_ptr<Shader>, const GraphicProgramCreateDesc& desc)
	{
		//do nothing but collect shader reflection info and create descriptorsets
		return false;
	}
	bool VulkanGraphicsRenderProgram::destroy()
	{
		return false;
	}
	bool VulkanGraphicsRenderProgram::apply(std::shared_ptr<CommandBuffer> cb)
	{
		return false;
	}
	IRenderProgramBinder& VulkanGraphicsRenderProgram::begin_bind()
	{
		return default;
	}
	bool VulkanGraphicsRenderProgram::end_bind()
	{
		return false;
	}
	bool VulkanGraphicsRenderProgram::refresh_pipeline()
	{
		return false;
	}
	bool VulkanGraphicsRenderProgram::apply_render_state(const std::function<void(RenderState*)>& fnRenderStateDefineCall)
	{
		return false;
	}
}