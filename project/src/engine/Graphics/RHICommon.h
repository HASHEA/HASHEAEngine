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
		ASH_FORMAT_B8G8R8A8_SRGB,
		ASH_FORMAT_COUNT
	} AshFormat;

	typedef enum AshColorSpace {
		ASH_COLOR_SPACE_UNDEFINED = 0,
		ASH_COLOR_SPACE_SRGB_NONLINEAR_KHR,
		ASH_COLOR_SPACE_COUNT
	} AshColorSpace;

	typedef enum AshPresentMode {
		ASH_PRESENT_MODE_UNDEFINED = 0,
		ASH_PRESENT_MODE_MAILBOX_KHR,
		ASH_PRESENT_MODE_IMMEDIATE_KHR,
		ASH_PRESENT_MODE_FIFO_KHR,
		ASH_PRESENT_MODE_COUNT
	} AshPresentMode;
}