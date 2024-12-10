#pragma once
#include "RHICommon.h"
#include "Base/hplatform.h"
#include "Base/hcore.h"
#include <Base/hmemory.h>
namespace RHI
{
	class RHIResource 
	{
	public:
		RHIResource() = default;
		~RHIResource() {};
	};

	class Texture;	
	struct TextureCreation;
	struct TextureViewCreation;

	class RHIView : public RHIResource
	{
	public:
		RHIView() = default;
		~RHIView() {};
	public:
	/*	virtual auto get_shader_resource_view() -> void* = 0;
		virtual auto get_unordered_access_view() -> void* = 0;*/

	};



	enum ASH_API AshImageType {
		Ash_Texture1D, Ash_Texture2D, Ash_Texture3D, Ash_TextureCube, Ash_Texture_1D_Array, Ash_Texture_2D_Array, Ash_Texture_Cube_Array, Count
	};
	namespace AshTextureFlags {
		enum Enum {
			Default, RenderTarget, Compute, Sparse, ShadingRate, Count
		};

		enum Mask {
			Default_mask = 1 << 0, RenderTarget_mask = 1 << 1, Compute_mask = 1 << 2, Sparse_mask = 1 << 3, ShadingRate_mask = 1 << 4
		};

		static const char* s_value_names[] = {
			"Default", "RenderTarget", "Compute", "Count"
		};

		static const char* to_string(Enum e) {
			return ((uint32_t)e < Enum::Count ? s_value_names[(int)e] : "unsupported");
		}

	} // namespace TextureFlags

	typedef enum AshImageViewType {
		ASH_IMAGE_VIEW_TYPE_1D = 0,
		ASH_IMAGE_VIEW_TYPE_2D = 1,
		ASH_IMAGE_VIEW_TYPE_3D = 2,
		ASH_IMAGE_VIEW_TYPE_CUBE = 3,
		ASH_IMAGE_VIEW_TYPE_1D_ARRAY = 4,
		ASH_IMAGE_VIEW_TYPE_2D_ARRAY = 5,
		ASH_IMAGE_VIEW_TYPE_CUBE_ARRAY = 6,
		ASH_IMAGE_VIEW_TYPE_MAX_ENUM = 0x7FFFFFFF
	} AshImageViewType;


}



