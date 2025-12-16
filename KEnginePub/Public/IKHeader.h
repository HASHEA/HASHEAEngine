#pragma once

#include <atomic>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

typedef int BOOL;

#ifndef TRUE
#define TRUE 1
#endif

#ifndef FALSE
#define FALSE 0
#endif

#define MAX_PATH 260
#include "KBase/Public/math/KMathPublic.h"
#include "KBase/Public/data_structure/KG_InterlockedVariable.h"
#include <mutex>
#include "KBase/Public/concurrency/KSpinMutext.h"
#include <string>

#ifdef _WIN32
#define ALIGN16_
#define ALIGN16_DECLARE(typ, def) __declspec(align(16)) typ def
#else
#define ALIGN16_DECLARE(typ, def) typ def __attribute__((aligned(16)))
#endif

#define MAX_OCLUSION_CULL_COUNT 5


enum KENUM_CULL_TYPE
{
    MAIN_CAMERA,
    SHADOWM_CASCADE0,
    SHADOWM_CASCADE1,
    SHADOWM_CASCADE2,

    SPOT_LIGHT_SHADOW0,
    SPOT_LIGHT_SHADOW1,

    TOP_CAMERA_WEATHER,
    // SHADOWM_CASCADE2,
    CULL_TYPE_COUNT,
};

namespace NSEngine
{
    enum class EModelRenderShadowShape : uint8_t
    {
        None = 0,
        Normal,
        Sphere,
    };
} // namespace NSEngine

#define PSSM_COUNT (KENUM_CULL_TYPE::SPOT_LIGHT_SHADOW0 - 1)

enum class TextureType : uint32_t
{
    Texture2D             = 0,
    Cubemap               = 1,
    Texture2DArray        = 2,
    Texture3D             = 3,
    CubemapArray          = 4,
    Texture1D             = 5,
    Texture1DArray        = 6,
    RWBuffer              = 7,  // uav buffer texel storage buffer for gimageBuffer in glsl
    MergedTexture2DArray  = 8,
    CombinedSamplerBuffer = 9,  // srv buffer
    TextureImage2D        = 10, // for image2D, feed it by storage image
    TextureImage3D        = 11,
    TextureImage2DArray   = 12,
    Count                 = 13, // 枚举数量

    Unknown = 255               // 无效类型
};

namespace gfx
{
    struct TimeStamp
    {
        std::string strLabel;
        float       fMicroseconds;
    };
}; // namespace gfx

#define SKIN_UBO_BIND_COUNT              10
#define SKIN_MODEL_INSTANCE_COUNT        80 // 每次最多画80个skinmodel
#define INSTANCE_MAT34_COUNT             80
#define INSTANCE_MAT44_COUNT             60
#define INSTANCE_VEC4_COUNT              240

#define VERTEX_POS_INDX                  0
#define VERTEX_NORMAL_INDX               1
#define VERTEX_COLOR_INDX                2
#define VERTEX_TANGENT_INDX              3
#define VERTEX_TEX0_INDX                 4
#define VERTEX_TEX1_INDX                 5
#define VERTEX_TEX2_INDX                 6
#define VERTEX_TEX3_INDX                 7
#define VERTEX_TEX4_INDX                 8
#define VERTEX_TEX5_INDX                 9

#define VERTEX_TEX6_INDX                 10
#define VERTEX_TEX7_INDX                 11
#define VERTEX_TEX8_INDX                 12
#define VERTEX_TEX9_INDX                 13

#define VERTEX_BLEND_INDX0               VERTEX_TEX4_INDX
#define VERTEX_BLEND_INDX1               VERTEX_TEX5_INDX

#define VERTEX_MATRIX_ROW1_INDX_INSTANCE VERTEX_TEX6_INDX
#define VERTEX_MATRIX_ROW2_INDX_INSTANCE VERTEX_TEX7_INDX
#define VERTEX_MATRIX_ROW3_INDX_INSTANCE VERTEX_TEX8_INDX
#define VERTEX_MATRIX_ROW4_INDX_INSTANCE VERTEX_TEX9_INDX

#define VERTEX_POINT_LIGHT_INDX_INSTANCE 15

// New Material
#define VERTEX_COLOR_INDX_INSTANCE       VERTEX_COLOR_INDX
#define VERTEX_UVINFO_INDX_INSTANCE      VERTEX_BLEND_INDX0

#define MOBILE_DYNAMIC_LIGHT

#define MACRO_X3D_ENABLE_SRT_MESHMODEL   0
#define MACRO_X3D_ENABLE_BAKED_MESHMODEL 1
#define MACRO_X3D_FORCE_GRASS_LOD1       1

#if defined(_WIN32) || defined(__MACOS__)
#define MACRO_X3D_ENABLE_TEX_STREAM 0
#else
#define MACRO_X3D_ENABLE_TEX_STREAM 0
#endif
#define MACRO_X3D_TEX_AUTO_RELEASE_GPU_FRAME_COUNT 300

#define BLOOM_MIP_COUNT         5
#define DOF_BLUR_COUNT          2
#define MAX_POSTRENDER_AB_COUNT 2




