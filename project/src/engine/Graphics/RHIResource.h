#pragma once
#include "RHICommon.h"
#include "Base/hplatform.h"
#include "Base/hcore.h"
#include <Base/hmemory.h>
namespace RHI
{

	class Texture;
	class TextureView;
	class Sampler;
	class Buffer;
	class CommandBuffer;
	class RenderPass;
	class Framebuffer;
	class Pipeline;

	struct RenderPassCreation;
	struct FramebufferCreation;
	struct PipelineCreation;
	struct SamplerCreation;
	struct TextureCreation;
	struct TextureViewCreation;
	struct BufferCreation;
	struct MapBufferParameters;
	class RHIResource 
	{
	public:
		RHIResource() = default;
		virtual ~RHIResource() {};
	public:
		virtual auto get_native_handle() -> void* = 0;
		virtual auto get_name() -> const char* = 0;
	public:
		bool immediate_deletion = false;
	};
	class RHIView : public RHIResource
	{
	public:
		RHIView() = default;
		virtual ~RHIView() {};
	public:
	/*	virtual auto get_shader_resource_view() -> void* = 0;
		virtual auto get_unordered_access_view() -> void* = 0;*/

	};



	
}



