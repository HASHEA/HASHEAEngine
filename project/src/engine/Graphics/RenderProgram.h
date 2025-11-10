#pragma once
#include "RHIResource.h"
#include "Base/hassert.h"
#include <functional>
namespace RHI
{
	class Shader;
	class CommandBuffer;
	class RenderState
	{

	};
	class IRenderProgramBinder
	{
	public:
		IRenderProgramBinder() = default;
		~IRenderProgramBinder() = default;
	};
	struct GraphicProgramCreateDesc
	{
	};
	class IGraphicsRenderProgram 
	{
	public:
		IGraphicsRenderProgram() = default;
		~IGraphicsRenderProgram() = default;
	public:
		virtual bool apply_render_state(const std::function<void(RenderState*)>& fnRenderStateDefineCall) = 0;
		virtual bool apply(std::shared_ptr<CommandBuffer> cb) = 0;
		//binding interface
		virtual IRenderProgramBinder& begin_bind() = 0;
		virtual bool end_bind() = 0;
	};
	class IComputeRenderProgram 
	{
	public:
		IComputeRenderProgram() = default;
		~IComputeRenderProgram() = default;
	};
	class IRayTracingRenderProgram
	{
	public:
		IRayTracingRenderProgram() = default;
		~IRayTracingRenderProgram() = default;
	};
}