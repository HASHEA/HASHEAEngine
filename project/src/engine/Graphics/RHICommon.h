#pragma once
#include "Base/hplatform.h"
#include "Base/hcore.h"
namespace RHI
{
	static const uint32_t                    k_invalid_resource = 0xffffffff;
	typedef uint32_t                         ResourceHandle;
	enum ASH_API AshTextureType {
		Ash_Texture1D, Ash_Texture2D, Ash_Texture3D, Ash_TextureCube, Ash_Texture_1D_Array, Ash_Texture_2D_Array, Ash_Texture_Cube_Array, Count
	};

	typedef enum AshFormat {
		ASH_FORMAT_UNDEFINED = 0,
	} AshFormat;
}