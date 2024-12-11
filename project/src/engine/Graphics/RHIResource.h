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


	struct SamplerCreation;
	struct TextureCreation;
	struct TextureViewCreation;
	class RHIResource 
	{
	public:
		RHIResource() = default;
		virtual ~RHIResource() {};
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



