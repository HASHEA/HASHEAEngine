#pragma once
#include "Base/hplatform.h"
#include "Base/hcore.h"
namespace RHI
{
	//for non immediately delete, just call smart pointer . reset() directly
#define RHI_RELEASE_IMMEDIATELY(res)\
{\
	(res)->immediate_deletion = true;\
	(res).reset();\
}

	enum ASH_API AshImageType {
		Ash_Texture1D, Ash_Texture2D, Ash_Texture3D, Ash_TextureCube, Ash_Texture_1D_Array, Ash_Texture_2D_Array, Ash_Texture_Cube_Array, Count
	};

	typedef struct AshDepthStencilValue {
		float       depth;
		uint32_t    stencil;
	} AshDepthStencilValue;

	typedef union AshColorValue
	{
		enum valueType
		{
			T_float32,
			T_int32,
			T_uint32
		};
		valueType v_type = T_float32;
		float float32[4];
		int32_t int32[4];
		uint32_t uint32[4];
		AshColorValue()
		{
			for (int i = 0; i < 4; ++i) {
				float32[i] = 0.0f; 
			}
			v_type = T_float32;
		};
		explicit AshColorValue(float value) {
			for (int i = 0; i < 4; ++i) {
				float32[i] = value;
			}
			v_type = T_float32;
		}
		explicit AshColorValue(int32_t value) {
			for (int i = 0; i < 4; ++i) {
				int32[i] = value;
			}
			v_type = T_int32;
		}
		explicit AshColorValue(uint32_t value) {
			for (int i = 0; i < 4; ++i) {
				uint32[i] = value;
			}
			v_type = T_uint32;
		}
		AshColorValue(float r, float g, float b, float a) {
			float32[0] = r; float32[1] = g; float32[2] = b; float32[3] = a;
			v_type = T_float32;
		}

		AshColorValue(int32_t r, int32_t g, int32_t b, int32_t a) {
			int32[0] = r; int32[1] = g; int32[2] = b; int32[3] = a;
			v_type = T_int32;
		}

		AshColorValue(uint32_t r, uint32_t g, uint32_t b, uint32_t a) {
			uint32[0] = r; uint32[1] = g; uint32[2] = b; uint32[3] = a;
			v_type = T_uint32;
		}
	};

	namespace AshResourceUsageType {
		enum Enum {
			Immutable, Dynamic, Stream, Staging, Readback, Count
		};

		enum Mask {
			Immutable_mask = 1 << 0, Dynamic_mask = 1 << 1, Stream_mask = 1 << 2, Staging_mask = 1 << 3, Readback_mask = 1 << 4, Count_mask = 1 << 5
		};

		static const char* s_value_names[] = {
			"Immutable", "Dynamic", "Stream", "Staging", "Count"
		};

		static const char* to_string(Enum e) {
			return ((uint32_t)e < Enum::Count ? s_value_names[(int)e] : "unsupported");
		}
	} // namespace ResourceUsageType

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

	namespace AshQueueType {
		enum Enum {
			Graphics, Compute, CopyTransfer, Ignored, Count
		};

		enum Mask {
			Graphics_mask = 1 << 0, Compute_mask = 1 << 1, CopyTransfer_mask = 1 << 2, Ignored_mask = 1 << 3,Count_mask = 1 << 4
		};

		static const char* s_value_names[] = {
			"Graphics", "Compute", "CopyTransfer","Ignored", "Count"
		};

		static const char* to_string(Enum e) {
			return ((uint32_t)e < Enum::Count ? s_value_names[(int)e] : "unsupported");
		}
	} // namespace QueueType

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



	typedef enum AshFormat {
		ASH_FORMAT_UNDEFINED = 0,
		ASH_FORMAT_R8_UNORM = 1,
		ASH_FORMAT_R8_UINT = 2,
		ASH_FORMAT_R8_SRGB = 3,
		ASH_FORMAT_R8G8_UNORM = 4,
		ASH_FORMAT_R8G8_UINT = 5,
		ASH_FORMAT_R8G8_SRGB = 6,
		ASH_FORMAT_R8G8B8_UNORM = 7,
		ASH_FORMAT_R8G8B8_UINT = 8,
		ASH_FORMAT_R8G8B8_SRGB = 9,
		ASH_FORMAT_B8G8R8_UNORM = 10,
		ASH_FORMAT_B8G8R8_UINT = 11,
		ASH_FORMAT_B8G8R8_SRGB = 12,
		ASH_FORMAT_R8G8B8A8_UNORM = 13,
		ASH_FORMAT_R8G8B8A8_UINT = 14,
		ASH_FORMAT_R8G8B8A8_SRGB = 15,
		ASH_FORMAT_B8G8R8A8_UNORM = 16,
		ASH_FORMAT_B8G8R8A8_UINT = 17,
		ASH_FORMAT_B8G8R8A8_SRGB = 18,
		ASH_FORMAT_R16_UNORM = 19,
		ASH_FORMAT_R16_UINT = 20,
		ASH_FORMAT_R16_SFLOAT = 21,
		ASH_FORMAT_R16G16_UNORM = 22,
		ASH_FORMAT_R16G16_UINT = 23,
		ASH_FORMAT_R16G16_SFLOAT = 24,
		ASH_FORMAT_R16G16B16_UNORM = 25,
		ASH_FORMAT_R16G16B16_UINT = 26,
		ASH_FORMAT_R16G16B16_SFLOAT = 27,
		ASH_FORMAT_R16G16B16A16_UNORM = 28,
		ASH_FORMAT_R16G16B16A16_UINT = 29,
		ASH_FORMAT_R16G16B16A16_SFLOAT = 30,
		ASH_FORMAT_R32_UINT = 31,
		ASH_FORMAT_R32_SINT = 32,
		ASH_FORMAT_R32_SFLOAT = 33,
		ASH_FORMAT_R32G32_UINT = 34,
		ASH_FORMAT_R32G32_SINT = 35,
		ASH_FORMAT_R32G32_SFLOAT = 36,
		ASH_FORMAT_R32G32B32_UINT = 37,
		ASH_FORMAT_R32G32B32_SINT = 38,
		ASH_FORMAT_R32G32B32_SFLOAT = 39,
		ASH_FORMAT_R32G32B32A32_UINT = 40,
		ASH_FORMAT_R32G32B32A32_SINT = 41,
		ASH_FORMAT_R32G32B32A32_SFLOAT = 42,
		ASH_FORMAT_R64_UINT = 43,
		ASH_FORMAT_R64_SINT = 44,
		ASH_FORMAT_R64_SFLOAT = 45,
		ASH_FORMAT_R64G64_UINT = 46,
		ASH_FORMAT_R64G64_SINT = 47,
		ASH_FORMAT_R64G64_SFLOAT = 48,
		ASH_FORMAT_R64G64B64_UINT = 49,
		ASH_FORMAT_R64G64B64_SINT = 50,
		ASH_FORMAT_R64G64B64_SFLOAT = 51,
		ASH_FORMAT_R64G64B64A64_UINT = 52,
		ASH_FORMAT_R64G64B64A64_SINT = 53,
		ASH_FORMAT_R64G64B64A64_SFLOAT = 54,
		ASH_FORMAT_D16_UNORM = 55,
		ASH_FORMAT_D32_SFLOAT = 56,
		ASH_FORMAT_S8_UINT = 57,
		ASH_FORMAT_D16_UNORM_S8_UINT = 58,
		ASH_FORMAT_D24_UNORM_S8_UINT = 59,
		ASH_FORMAT_D32_SFLOAT_S8_UINT = 60,
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
		ASH_PRESENT_MODE_FIFO_RELAXED_KHR,
		ASH_PRESENT_MODE_SHARED_DEMAND_REFRESH_KHR,
		ASH_PRESENT_MODE_SHARED_CONTINUOUS_REFRESH_KHR,
		ASH_PRESENT_MODE_COUNT
	} AshPresentMode;

	typedef enum AshSamplerAddressMode {
		ASH_SAMPLER_ADDRESS_MODE_REPEAT = 0,
		ASH_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT = 1,
		ASH_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE = 2,
		ASH_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER = 3,
		ASH_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE = 4,
		ASH_SAMPLER_ADDRESS_MODE_MAX_ENUM = 0x7FFFFFFF
	} AshSamplerAddressMode;

	typedef enum AshBorderColor {
		ASH_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK = 0,
		ASH_BORDER_COLOR_INT_TRANSPARENT_BLACK = 1,
		ASH_BORDER_COLOR_FLOAT_OPAQUE_BLACK = 2,
		ASH_BORDER_COLOR_INT_OPAQUE_BLACK = 3,
		ASH_BORDER_COLOR_FLOAT_OPAQUE_WHITE = 4,
		ASH_BORDER_COLOR_INT_OPAQUE_WHITE = 5,
		ASH_BORDER_COLOR_FLOAT_CUSTOM_EXT = 1000287003,
		ASH_BORDER_COLOR_INT_CUSTOM_EXT = 1000287004,
		ASH_BORDER_COLOR_MAX_ENUM = 0x7FFFFFFF
	} AshBorderColor;

	typedef enum AshFilter {
		ASH_FILTER_NEAREST = 0,
		ASH_FILTER_LINEAR = 1,
		ASH_FILTER_CUBIC_EXT = 1000015000,//only for min max
		ASH_FILTER_MAX_ENUM = 0x7FFFFFFF
	} AshFilter;

	typedef enum AshCompareOp {
		ASH_COMPARE_OP_NEVER = 0,
		ASH_COMPARE_OP_LESS = 1,
		ASH_COMPARE_OP_EQUAL = 2,
		ASH_COMPARE_OP_LESS_OR_EQUAL = 3,
		ASH_COMPARE_OP_GREATER = 4,
		ASH_COMPARE_OP_NOT_EQUAL = 5,
		ASH_COMPARE_OP_GREATER_OR_EQUAL = 6,
		ASH_COMPARE_OP_ALWAYS = 7,
		ASH_COMPARE_OP_MAX_ENUM = 0x7FFFFFFF
	} AshCompareOp;

	typedef enum AshSamplerReductionMode {
		ASH_SAMPLER_REDUCTION_MODE_WEIGHTED_AVERAGE = 0,
		ASH_SAMPLER_REDUCTION_MODE_MIN = 1,
		ASH_SAMPLER_REDUCTION_MODE_MAX = 2,
		ASH_SAMPLER_REDUCTION_MODE_MAX_ENUM = 0x7FFFFFFF
	} AshSamplerReductionMode;

	typedef enum AshBufferUsageFlagBits {
		ASH_BUFFER_USAGE_TRANSFER_SRC_BIT = 0x00000001,
		ASH_BUFFER_USAGE_TRANSFER_DST_BIT = 0x00000002,
		ASH_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT = 0x00000004,
		ASH_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT = 0x00000008,
		ASH_BUFFER_USAGE_UNIFORM_BUFFER_BIT = 0x00000010,
		ASH_BUFFER_USAGE_STORAGE_BUFFER_BIT = 0x00000020,
		ASH_BUFFER_USAGE_INDEX_BUFFER_BIT = 0x00000040,
		ASH_BUFFER_USAGE_VERTEX_BUFFER_BIT = 0x00000080,
		ASH_BUFFER_USAGE_INDIRECT_BUFFER_BIT = 0x00000100,
		ASH_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT = 0x00020000,
		ASH_BUFFER_USAGE_VIDEO_DECODE_SRC_BIT_KHR = 0x00002000,
		ASH_BUFFER_USAGE_VIDEO_DECODE_DST_BIT_KHR = 0x00004000,
		ASH_BUFFER_USAGE_TRANSFORM_FEEDBACK_BUFFER_BIT_EXT = 0x00000800,
		ASH_BUFFER_USAGE_TRANSFORM_FEEDBACK_COUNTER_BUFFER_BIT_EXT = 0x00001000,
		ASH_BUFFER_USAGE_CONDITIONAL_RENDERING_BIT_EXT = 0x00000200,
		ASH_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR = 0x00080000,
		ASH_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR = 0x00100000,
		ASH_BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR = 0x00000400,
		ASH_BUFFER_USAGE_SAMPLER_DESCRIPTOR_BUFFER_BIT_EXT = 0x00200000,
		ASH_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT = 0x00400000,
		ASH_BUFFER_USAGE_PUSH_DESCRIPTORS_DESCRIPTOR_BUFFER_BIT_EXT = 0x04000000,
		ASH_BUFFER_USAGE_MICROMAP_BUILD_INPUT_READ_ONLY_BIT_EXT = 0x00800000,
		ASH_BUFFER_USAGE_MICROMAP_STORAGE_BIT_EXT = 0x01000000,
		ASH_BUFFER_USAGE_FLAG_BITS_MAX_ENUM = 0x7FFFFFFF
	} AshBufferUsageFlagBits;

	typedef enum AshCommandBufferState
	{
		ASH_Idle,
		ASH_Recording,
		ASH_Ended,
		ASH_Submitted,
		ASH_State_Count
	}AshCommandBufferState;

	typedef enum AshResourceState {
		ASH_RESOURCE_STATE_UNDEFINED = 0,
		ASH_RESOURCE_STATE_GENERAL = 1,
		ASH_RESOURCE_STATE_RENDER_TARGET = 2,
		ASH_RESOURCE_STATE_DEPTH_STENCIL_WRITE = 3,
		ASH_RESOURCE_STATE_DEPTH_STENCIL_READ = 4,
		ASH_RESOURCE_STATE_SHADER_RESOURCE = 5,
		ASH_RESOURCE_STATE_COPY_SOURCE = 6,
		ASH_RESOURCE_STATE_COPY_DEST = 7,
		ASH_RESOURCE_STATE_PREINITIALIZED = 8,
		ASH_RESOURCE_STATE_UNORDERED_ACCESS = 9,
		ASH_RESOURCE_STATE_DEPTH_WRITE,
		ASH_RESOURCE_STATE_DEPTH_READ,
		ASH_RESOURCE_STATE_STENCIL_WRITE,
		ASH_RESOURCE_STATE_STENCIL_READ,
		ASH_RESOURCE_STATE_PRESENT,
		ASH_RESOURCE_STATE_FRAGMENT_SHADING_RATE_ATTACHMENT,
		ASH_RESOURCE_STATE_MAX_ENUM = 0x7FFFFFFF
	} AshResourceState;

	typedef enum AshSamplerState {
		ASH_SAMPLER_STATE_DEFAULT = 0,
		ASH_SAMPLER_STATE_MAX_ENUM
	} AshSamplerState;

	typedef enum AshLoadOption {
		ASH_LOAD_DONT_CARE,
		ASH_LOAD_LOAD,
		ASH_LOAD_CLEAR,
		ASH_LOAD_MAX_ENUM
	} AshLoadOption;
}



