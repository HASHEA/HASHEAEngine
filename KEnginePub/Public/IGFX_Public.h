////////////////////////////////////////////////////////////////////////////////
//
//  FileName    : IGFX_Public.h
//  Creator     : HuaFei
//  Create Date : 2024-12
//
////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <cstdint>
#include <initializer_list>
#include <atomic>
#include <functional>
#include <unordered_map>
#include "KBase/Public/math/KMathPublic.h"
#include "KBase/Public/io/KByteStream.h"
#include "KBase/Public/io/KMetaData.h"
#include "KEnginePub/Public/IKHeader.h"
#include "Engine/KUniqueString.h"
#include "KEsDrv.h"
#include <Engine/KGLog.h>
#include <array>
#include "KBase/Public/async_task/KAsyncTask.h"
#include "IKVertexInputDefine.h"
#ifdef _WIN32
typedef struct HWND__* HWND;
typedef struct HDC__* HDC;
typedef struct HGLRC__* HGLRC;
#endif

#if defined(__APPLE__) || defined(_WIN32)
#define DEFAULT_SHADOWMAP_FMT     gfx::TEX_FORMAT_D16_UNORM
#else
#define DEFAULT_SHADOWMAP_FMT     gfx::TEX_FORMAT_D16_UNORM
#endif

#if UNITY_PLUGINS
#define WGL_DRV 1
#else
#define WGL_DRV 0
#endif

#define UNUSED_FILE_PATH ""
#define UNUSED_MACRO     ""

#ifndef VERTEX_BUFFER_BIND_ID0
#define VERTEX_BUFFER_BIND_ID0 0
#endif

#ifndef VERTEX_BUFFER_BIND_ID1
#define VERTEX_BUFFER_BIND_ID1 1
#endif

#ifndef VERTEX_BUFFER_BIND_ID2
#define VERTEX_BUFFER_BIND_ID2 2
#endif

#ifndef VERTEX_BUFFER_BIND_ID3
#define VERTEX_BUFFER_BIND_ID3 3
#endif

#ifndef INSTANCE_BUFFER_BIND_ID
#define INSTANCE_BUFFER_BIND_ID 4
#endif

#define KMAX_BIND_VERT_STREAM 5
#define KMAX_BLEND_ATTACHMENT 5

#define WHOLE_SIZE            (~0ULL)
#define QUEUE_FAMILY_IGNORED  (~0U)

#if defined(__APPLE__)
#define DYNAMIC_COHERENT_GFXBUFFER 0
#else
#define DYNAMIC_COHERENT_GFXBUFFER 0
#endif

#define MACRO_X3D_DISABLE_SHADER_EFFECT_CACHE 1
#define MAX_MIP_COUNT                         12
#define MAX_RENDERTARGET_COUNT                8

#if defined(_WIN32) || defined(__MACOS__)
#define BLANK_TEX_PATH         "enginedata/public/white.dds"
#define BLACK_TEX_PATH         "enginedata/public/white.dds"
#define ERROR_TEX_PATH         "enginedata/public/errortexture.dds"
#define DEFAULT_NORMALTEX_PATH "enginedata/public/default_normal.dds"
#define ERROR_TEX_ARRAY_PATH   "enginedata/public/errortexturearray.dds"
#define ERROR_TEX_CUBE_PATH    "enginedata/public/errortexturecube.dds"
#else
#define BLANK_TEX_PATH         "enginedata/public/white.ktx"
#define BLACK_TEX_PATH         "enginedata/public/white.ktx"
#define ERROR_TEX_PATH         "enginedata/public/errortexture.ktx"
#define DEFAULT_NORMALTEX_PATH "enginedata/public/default_normal.ktx"
#define ERROR_TEX_ARRAY_PATH   "enginedata/public/errortexturearray.ktx"
#define ERROR_TEX_CUBE_PATH    "enginedata/public/errortexturecube.ktx"
#endif

#define COLLISION_WITH_TEX_PREFIX "alpha_001."

#define PER_MTL_UBO_NAME_0        "MaterialLocalParams"
#define PER_MTL_UBO_NAME_1        "PerMTLUBO"

#define RHI_ENUM_CLASS_OPERATORS(e_) \
    static_assert(std::is_enum<e_>::value, "Must be enum type"); \
    using _T = std::underlying_type_t<e_>; \
    inline e_ operator&(e_ a, e_ b) { return static_cast<e_>(static_cast<_T>(a) & static_cast<_T>(b)); } \
    inline e_ operator|(e_ a, e_ b) { return static_cast<e_>(static_cast<_T>(a) | static_cast<_T>(b)); } \
    inline e_& operator|=(e_& a, e_ b) { a = a | b; return a; } \
    inline e_& operator&=(e_& a, e_ b) { a = a & b; return a; } \
    inline e_ operator~(e_ a) { return static_cast<e_>(~static_cast<_T>(a)); } \
    inline bool is_set(e_ val, e_ flag) { return (val & flag) != static_cast<e_>(0); } \
    inline void flip_bit(e_& val, e_ flag) { val = is_set(val, flag) ? (val & (~flag)) : (val | flag); }


#define RENDER_GRAPH_ENUM_INFO(T, ...)                                         \
    struct T##_info                                                            \
    {                                                                          \
        static std::vector<std::pair<T, std::string>>& items()                 \
        {                                                                      \
            static std::vector<std::pair<T, std::string>> items = __VA_ARGS__; \
            return items;                                                      \
        }                                                                      \
    };

namespace NSEngine
{
    // 用常量定义，避免和其他地方的定义冲突
    const int MAX_PATH_LEN = 1024;
    class K3DScene;
    class KWindowInfo;
    class KGlobalUBO;
} // namespace NSEngine

namespace NSRender
{
    class KRenderScene;
    class KRenderSceneView;
    class KRenderSceneCullResult;
} // namespace NSRender

class IKRender;

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// 新封装的接口

#define STAGE_BUFFER_MATCH_FIT_SIZE 262144 // 256kb
#define TERRAIN_POS_DEBUG           0
#define DEVICE_OBJECT_NAME_LEN      64

#if defined(_WIN32) && defined(_DEBUG)
#define DESCRIPTORSET_VALIDATE 0
#else
#define DESCRIPTORSET_VALIDATE 0
#endif

enum class KEnumMtlTaskLevel
{
    DISABLE_MTL_THREAD,
    NORMAL_MTL_THREAD_LEVEL,
    HIGH_MTL_THREAD_LEVEL,
    ABOVE_HIGH_MTL_THREAD_LEVEL,
};

struct ESContext // deprecated_remove_later
{
    /// Put platform specific data here
    void* platformData{ nullptr };

    /// Put your user data here...
    void* userData{ nullptr };

    /// Window width
    uint32_t width{ 0 };

    /// Window height
    uint32_t height{ 0 };

    /// Swapchain width
    uint32_t swapchainWidth{ 0 };

    /// Swapchain height
    uint32_t swapchainHeight{ 0 };

    /// Window width with some limit by engine
    uint32_t screenWidth{ 0 };

    /// Window height with some limit by engine
    uint32_t screenHeight{ 0 };

    uint32_t renderWidth{ 0 };
    uint32_t renderHeight{ 0 };

#ifndef __APPLE__
#if defined(_WIN32) || defined(__ANDROID__) || defined(__OHOS__)
#if WGL_DRV
    HWND  eglNativeWindow{ nullptr };
    HDC   eglNativeDisplay{ nullptr };
    HGLRC eglContext{ nullptr };
#else
    ///// Window handle
    void* eglNativeWindow{ nullptr }; // hwnd

    /// Display handle
    void* eglNativeDisplay{ nullptr }; // hdc

    ///// EGL display
    void* eglDisplay{ nullptr };

    ///// EGL context
    void* eglContext{ nullptr };

    ///// EGL surface
    void* eglSurface{ nullptr };
    void* config{ nullptr };
#endif
#endif
#else
    void* eglNativeWindow; // hwnd
#endif
};

#define const_pool_str          const char*
#define const_param_name        const char*
#define static_const_param_name static const char*
const_pool_str GetParamNameByPool(const char* pName);

#define DECLARE_PARAM_NAME(s) static_const_param_name s = GetParamNameByPool(#s)

// #define CompareParam(s0, s1) (s0 == s1 || strcmp(s0, s1) == 0)
#define CompareParam(s0, s1)  (s0 == s1)

#ifdef __ANDROID__ // VK_USE_PLATFORM_ANDROID_KHR
struct ANativeWindow;
#endif

class IKGFX_PipelineLoadThread;

namespace gfx
{
    class KRenderState;
    class KGfxTexture;

    struct KGfxBarrier;

    struct KGFX_BufferViewDesc;

    class IKGFX_GraphicDevice;
    class IKGFX_Buffer;
    class IKGFX_BufferView;
    class IKGFX_RenderContext;
    class IKGFX_RenderFrameBuffer;

    struct KGlobalUBO;
    struct KGFX_PHYSICAL_DEVICE_LIMITS
    {
        uint32_t maxImageDimension1D;
        uint32_t maxImageDimension2D;
        uint32_t maxImageDimension3D;
        uint32_t maxImageDimensionCube;
        uint32_t maxImageArrayLayers;
        uint32_t maxTexelBufferElements;
        uint32_t maxUniformBufferRange;
        uint32_t maxStorageBufferRange;
        uint32_t maxPushConstantsSize;
        uint32_t maxMemoryAllocationCount;
        uint32_t maxSamplerAllocationCount;
        uint64_t bufferImageGranularity;
        uint64_t sparseAddressSpaceSize;
        uint32_t maxBoundDescriptorSets;
        uint32_t maxPerStageDescriptorSamplers;
        uint32_t maxPerStageDescriptorUniformBuffers;
        uint32_t maxPerStageDescriptorStorageBuffers;
        uint32_t maxPerStageDescriptorSampledImages;
        uint32_t maxPerStageDescriptorStorageImages;
        uint32_t maxPerStageDescriptorInputAttachments;
        uint32_t maxPerStageResources;
        uint32_t maxDescriptorSetSamplers;
        uint32_t maxDescriptorSetUniformBuffers;
        uint32_t maxDescriptorSetUniformBuffersDynamic;
        uint32_t maxDescriptorSetStorageBuffers;
        uint32_t maxDescriptorSetStorageBuffersDynamic;
        uint32_t maxDescriptorSetSampledImages;
        uint32_t maxDescriptorSetStorageImages;
        uint32_t maxDescriptorSetInputAttachments;
        uint32_t maxVertexInputAttributes;
        uint32_t maxVertexInputBindings;
        uint32_t maxVertexInputAttributeOffset;
        uint32_t maxVertexInputBindingStride;
        uint32_t maxVertexOutputComponents;
        uint32_t maxTessellationGenerationLevel;
        uint32_t maxTessellationPatchSize;
        uint32_t maxTessellationControlPerVertexInputComponents;
        uint32_t maxTessellationControlPerVertexOutputComponents;
        uint32_t maxTessellationControlPerPatchOutputComponents;
        uint32_t maxTessellationControlTotalOutputComponents;
        uint32_t maxTessellationEvaluationInputComponents;
        uint32_t maxTessellationEvaluationOutputComponents;
        uint32_t maxGeometryShaderInvocations;
        uint32_t maxGeometryInputComponents;
        uint32_t maxGeometryOutputComponents;
        uint32_t maxGeometryOutputVertices;
        uint32_t maxGeometryTotalOutputComponents;
        uint32_t maxFragmentInputComponents;
        uint32_t maxFragmentOutputAttachments;
        uint32_t maxFragmentDualSrcAttachments;
        uint32_t maxFragmentCombinedOutputResources;
        uint32_t maxComputeSharedMemorySize;
        uint32_t maxComputeWorkGroupCount[3];    // Dispatch在每个维度上可以启动的最大工作组数量
        uint32_t maxComputeWorkGroupInvocations; // 工作组中最多可以启动的总线程数
        uint32_t maxComputeWorkGroupSize[3];     // 工作组在每个维度上最大支持的线程数
        uint32_t subPixelPrecisionBits;
        uint32_t subTexelPrecisionBits;
        uint32_t mipmapPrecisionBits;
        uint32_t maxDrawIndexedIndexValue;
        uint32_t maxDrawIndirectCount;
        float    maxSamplerLodBias;
        float    maxSamplerAnisotropy;
        uint32_t maxViewports;
        uint32_t maxViewportDimensions[2];
        float    viewportBoundsRange[2];
        uint32_t viewportSubPixelBits;
        size_t   minMemoryMapAlignment;
        uint64_t minTexelBufferOffsetAlignment;
        uint64_t minUniformBufferOffsetAlignment;
        uint64_t minStorageBufferOffsetAlignment;
        int32_t  minTexelOffset;
        uint32_t maxTexelOffset;
        int32_t  minTexelGatherOffset;
        uint32_t maxTexelGatherOffset;
        float    minInterpolationOffset;
        float    maxInterpolationOffset;
        uint32_t subPixelInterpolationOffsetBits;
        uint32_t maxFramebufferWidth;
        uint32_t maxFramebufferHeight;
        uint32_t maxFramebufferLayers;
        uint32_t framebufferColorSampleCounts;
        uint32_t framebufferDepthSampleCounts;
        uint32_t framebufferStencilSampleCounts;
        uint32_t framebufferNoAttachmentsSampleCounts;
        uint32_t maxColorAttachments;
        uint32_t sampledImageColorSampleCounts;
        uint32_t sampledImageIntegerSampleCounts;
        uint32_t sampledImageDepthSampleCounts;
        uint32_t sampledImageStencilSampleCounts;
        uint32_t storageImageSampleCounts;
        uint32_t maxSampleMaskWords;
        uint32_t timestampComputeAndGraphics;
        float    timestampPeriod;
        uint32_t maxClipDistances;
        uint32_t maxCullDistances;
        uint32_t maxCombinedClipAndCullDistances;
        uint32_t discreteQueuePriorities;
        float    pointSizeRange[2];
        float    lineWidthRange[2];
        float    pointSizeGranularity;
        float    lineWidthGranularity;
        uint32_t strictLines;
        uint32_t standardSampleLocations;
        uint64_t optimalBufferCopyOffsetAlignment;
        uint64_t optimalBufferCopyRowPitchAlignment;
        uint64_t nonCoherentAtomSize;
        KGFX_PHYSICAL_DEVICE_LIMITS()
        {
            memset(this, 0, sizeof(KGFX_PHYSICAL_DEVICE_LIMITS));
        }
    };

  
    enum class XRActionType
    {
        POSE_ACIVE_ACTION_XR,
        VIBRATE_ACTION_XR,

        THUMBSTICK_VALUE_XR,
        THUMBSTICK_CLICK_XR,
        THUMBSTICK_TOUCH_XR,
        THUMBSTICK_RESET_XR,

        TRIGGER_VALUE_XR,
        SQUEEZE_VALUE_XR,

        INPUT_A_XR,
        INPUT_B_XR,
        INPUT_X_XR,
        INPUT_Y_XR,

        MENU_XR
    };

    enum class XRActionSide
    {
        LEFT_SIDE,
        RIGHT_SIDE,
    };

    struct IXRActionListener
    {
    public:
        virtual BOOL DoActionValue(XRActionType actionType, XRActionSide side, float pFloatValue, float pFloat2_x, float pFloat2_y) = 0;
    };

    enum enumGraphicContext : uint8_t
    {
        MAIN_CONTEXT,
        // RESVIEW_CONTEX,
        CONTEXT_COUNT = 8
    };

    enum enumTextureType : uint8_t
    {
        FILE_TEXTURE,
        RTT2D_TEXTURE,
        MEM_TEXTURE,
        RAW_TEXTURE,
        MASK_TEXTURE,
        GEN_TEXTURE,
        FILE_MERGED_TEXTURE_ARRAY,
    };

    enum enumDescriptorType : uint32_t
    {
        DESCRIPTOR_TYPE_SAMPLER,
        DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        DESCRIPTOR_TYPE_SAMPLED_IMAGE,
        DESCRIPTOR_TYPE_STORAGE_IMAGE,
        DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER,
        DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER,
        DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        DESCRIPTOR_TYPE_STORAGE_BUFFER,
        DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
        DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC,
        DESCRIPTOR_TYPE_INPUT_ATTACHMENT,
        DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE
    };

    enum enumImageLayout : uint8_t
    {
        IMAGE_LAYOUT_UNDEFINED,
        IMAGE_LAYOUT_GENERAL,
        IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
        IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        IMAGE_LAYOUT_PRESENT_SRC_KHR,
        IMAGE_LAYOUT_SHARED_PRESENT_KHR
    };

    enum enumAccessFlag : uint8_t
    {
        ACCESS_NONE = 0,
        ACCESS_INDIRECT_COMMAND_READ_BIT,
        ACCESS_INDEX_READ_BIT,
        ACCESS_VERTEX_ATTRIBUTE_READ_BIT,
        ACCESS_UNIFORM_READ_BIT,
        ACCESS_INPUT_ATTACHMENT_READ_BIT,
        ACCESS_SHADER_BIT,
        ACCESS_SHADER_READ_BIT,
        ACCESS_SHADER_WRITE_BIT,
        ACCESS_COLOR_ATTACHMENT,
        ACCESS_DEPTH_STENCIL_ATTACHMENT,
        ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE,
        ACCESS_TRANSFER_READ_BIT,
        ACCESS_TRANSFER_WRITE_BIT,
        ACCESS_HOST_READ_BIT,
        ACCESS_HOST_WRITE_BIT,
        ACCESS_MEMORY_READ_BIT,
        ACCESS_MEMORY_WRITE_BIT,
        ACCESS_COMMAND_PROCESS_READ_BIT_NVX,
        ACCESS_COMMAND_PROCESS_WRITE_BIT_NVX,
        ACCESS_COLOR_ATTACHMENT_READ_NONCOHERENT_BIT_EXT,
        ACCESS_VERTEX_INDEX_READ_BIT,
        ACCESS_FLAG_BITS_MAX_ENUM
    };

    enum enumIndexType : uint8_t
    {
        INDEX_TYPE_UINT16,
        INDEX_TYPE_UINT32,
        INDEX_TYPE_MAX_NUM
    };

    enum enumVertexFormat : uint8_t
    {
        VERT_FORMAT_R32G32B32A32_SFLOAT,
        VERT_FORMAT_R32G32B32_SFLOAT,
        VERT_FORMAT_R32G32_SFLOAT,
        VERT_FORMAT_R32_SFLOAT,
        VERT_FORMAT_R8G8B8A8_UINT,
        VERT_FORMAT_R8G8B8A8_SINT,
        VERT_FORMAT_R8G8B8A8_UNORM,
        VERT_FORMAT_R8G8B8A8_SNORM,
        VERT_FORMAT_R16G16_UINT,
        VERT_FORMAT_R16G16_SINT,
        VERT_FORMAT_COUNT
    };

    enum enumProgramDataType : uint8_t
    {
        FLOAT1_TYPE,
        FLOAT2_TYPE,
        FLOAT3_TYPE,
        FLOAT4_TYPE,
        FLOAT4X4_TYPE,

        FLOAT1_ARRAY_TYPE,
        FLOAT2_ARRAY_TYPE,
        FLOAT3_ARRAY_TYPE,
        FLOAT4_ARRAY_TYPE,
        FLOAT4X4_ARRAY_TYPE,

        INT1_TYPE,
        INT2_TYPE,
        INT3_TYPE,
        INT4_TYPE,
        INT_ARRAY_TYPE,

        UINT1_TYPE,
        UINT2_TYPE,
        UINT3_TYPE,
        UINT4_TYPE,
        UINT_ARRAY_TYPE,

        IMAGE_TEXTURE,
        IMAGE_TEXTURES,
        USER_TYPE,
        DATA_TYPE_COUNT
    };

    enum enumUniformBaseType : uint8_t
    {
        BASE_FLOAT,
        BASE_INT,
        BASE_BOOL,
        BASE_DOUBLE,
        BASE_UINT,
        BASE_INT64,
        BASE_UINT64,
        BASE_SAMPLER,
        BASE_OTHER
    };


    class KProgramAttribute
    {
    public:
        const_pool_str           szName = nullptr;
        gfx::enumProgramDataType type = gfx::USER_TYPE;
        bool                     bInstanceData = false;
        uint32_t                 uSize = 0;
        gfx::enumVertexFormat    fmt = gfx::VERT_FORMAT_COUNT;
        int                      nLocation = 0; // gfx::KAttribUsage::Enum
        gfx::KAttribUsage::Enum  vertexUsage = {};
        BOOL                     Save(KByteBufferStream& byteStream);
        BOOL                     Load(KByteBufferStream& byteStream);
    };

    uint32_t                GetProgramDataTypeSize(gfx::enumProgramDataType t, uint32_t uArrayCount);
    gfx::enumVertexFormat   GetVertFormat(gfx::enumProgramDataType t, gfx::KAttribUsage::Enum usage);
    uint32_t                GetBaseTypeSize(gfx::enumUniformBaseType b);
    gfx::KAttribUsage::Enum GetKAttribUsage(const_pool_str pName);

    enum enumUniformType : uint8_t
    {
        UBO_UNIFORM,
        SSBO_UNIFORM,
        PUSH_CONSTANT_UNIFORM,
        SPEICALIZATION_CONST_UNIFORM, // dx需要这个来模拟vk，vk里面没这个类型
        TEXTURE_UNIFORM,
        SAMPLER_UNIFORM,
        ACCELERATION_STRUCTURE_UNIFORM,
        UniformType_COUNT,
    };

    static const char* g_unifromTypeName[UniformType_COUNT] =
    {
        "UBO_UNIFORM",
        "SSBO_UNIFORM",
        "PUSH_CONSTANT_UNIFORM",
        "SPEICALIZATION_CONST_UNIFORM"
        "TEXTURE_UNIFORM",
        "SAMPLER_UNIFORM"
        "ACCELERATION_STRUCTURE_UNIFORM"
    };

    static_assert(
        sizeof(g_unifromTypeName) / sizeof(g_unifromTypeName[0]) == (uint32_t)UniformType_COUNT,
        "enumUniformType g_unifromTypeName must have same size"
        );

    /*
    enum KEnumUniformScopeType
    {
        LOCAL_UNIFORM_SCOPE,
        PER_MTL_UNIFORM_SCOPE,
        GLOBAL_STANDARD_UBO,
    };
    */

    enum enumPipelineBindPoint : uint8_t
    {
        PIPELINE_BIND_POINT_GRAPHICS,
        PIPELINE_BIND_POINT_COMPUTE,
        PIPELINE_BIND_POINT_RAY_TRACNG
    };

    enum enumLoadActionType : uint8_t
    {
        LOAD_ACTION_DONTCARE,
        LOAD_ACTION_LOAD,
        LOAD_ACTION_CLEAR,
        LOAD_ACTION_NONE,
        MAX_LOAD_ACTION
    };

    enum enumStoreActionType : uint8_t
    {
        STORE_ACTION_STORE,
        STORE_ACTION_DONTCARE,
        STORE_ACTION_NONE,
        MAX_STORE_ACTION
    };

    enum class EnuRenderProcess
    {
        UNDEFINED,
        MAINSCENE,
        MINISCENE,
        UISFX,
        POP_MINISCENE
    };

    enum enumPolygonMode : uint8_t
    {
        POLYGON_MODE_FILL,
        POLYGON_MODE_LINE,
        POLYGON_MODE_POINT
    };

    enum enumFrontFaceMode : uint8_t
    {
        FRONT_FACE_COUNTER_CLOCKWISE, // ccw
        FRONT_FACE_CLOCKWISE,         // cw
    };

    enum enumCullMode : uint8_t
    {
        CULL_MODE_NONE,
        CULL_MODE_FRONT,
        CULL_MODE_BACK,
        CULL_MODE_FRONT_AND_BACK
    };

    enum enumDrawMode : uint8_t
    {
        PT_POINT_LIST,
        PT_LINE_LIST,
        PT_LINE_STRIP,
        PT_TRIANGLE_LIST,
        PT_TRIANGLE_STRIP,
        PT_TRIANGLE_FAN,
        PT_PATCH
    };

    enum enumDepthType : uint8_t
    {
        DEPTH_TEST_LESS,
        DEPTH_TEST_LEQUAL,
        DEPTH_TEST_EQUAL,
        DEPTH_TEST_GEQUAL,
        DEPTH_TEST_GREATER,
        DEPTH_TEST_NOTEQUAL,
        DEPTH_TEST_NEVER,
        DEPTH_TEST_ALWAYS,
    };

    enum enumStencilType : uint8_t
    {
        STENCIL_TEST_LESS,
        STENCIL_TEST_LEQUAL,
        STENCIL_TEST_EQUAL,
        STENCIL_TEST_GEQUAL,
        STENCIL_TEST_GREATER,
        STENCIL_TEST_NOTEQUAL,
        STENCIL_TEST_NEVER,
        STENCIL_TEST_ALWAYS
    };

    enum enumStencilOpType : uint8_t
    {
        STENCIL_OP_KEEP = 0,
        STENCIL_OP_ZERO = 1,
        STENCIL_OP_REPLACE = 2,
        STENCIL_OP_INCREMENT_AND_CLAMP = 3,
        STENCIL_OP_DECREMENT_AND_CLAMP = 4,
        STENCIL_OP_INVERT = 5,
        STENCIL_OP_INCREMENT_AND_WRAP = 6,
        STENCIL_OP_DECREMENT_AND_WRAP = 7,
    };

    enum enumBlendType : uint8_t
    {
        BLEND_ZERO,                     //(0, 0, 0)                   0
        BLEND_ONE,                      //(1, 1, 1)                   1
        BLEND_SRC_COLOR,                //(Rs,Gs,Bs)                  As
        BLEND_ONE_MINUS_SRC_COLOR,      //(1.1,1)-(Rs,Gs,Bs)          1-As
        BLEND_DST_COLOR,                //(Rd,Gd,Bd)                  Ad
        BLEND_ONE_MINUS_DST_COLOR,      //(1,1,1)-(Rd,Gd,Bd)          1-Ad
        BLEND_SRC_ALPHA,                //(As,As,As)                  As
        BLEND_ONE_MINUS_SRC_ALPHA,      //(1,1,1) - (As,As,As)        1-As
        BLEND_DST_ALPHA,                //(Ad,Ad,Ad)                  Ad
        BLEND_ONE_MINUS_DST_ALPHA,      //(1,1,1)-(Ad,Ad,Ad)          1-Ad
        BLEND_CONSTANT_COLOR,           //(Rc,Gc,Bc)                  Ac
        BLEND_ONE_MINUS_CONSTANT_COLOR, //(1,1,1)-(Rc,Gc,Bc)          1-Ac
        BLEND_CONSTANT_ALPHA,           //(Ac,Ac,Ac)                  Ac
        BLEND_ONE_MINUS_CONSTANT_ALPHA, //(1,1,1)-(Ac,Ac,Ac)          1-Ac
        BLEND_SRC_ALPHA_SATURATE        //(f,f,f);f=min(As,1-Ad)      1
    };

    enum enumBlendEquationType : uint8_t
    {
        BLEND_EQUATION_ADD,    // Cs*S+Cd*D
        BLEND_EQUATION_SUB,    // Cs*S-Cd*D
        BLEND_EQUATION_REVSUB, // Cd*D-Cs*S
        BLEND_EQUATION_MIN,    // min(Cs*S, Cd*D)
        BLEND_EQUATION_MAX     // max(Cs*S, Cd*D)
    };

    struct CustomRenderState
    {
        bool              bStencilTestEnable = false;
        enumStencilType   eStencilCompareOp = STENCIL_TEST_NEVER;
        short             nReference = 0;
        uint32_t          uMask = 0;
        enumStencilOpType eStencilFailOP = STENCIL_OP_KEEP;
        enumStencilOpType eStencilDepthFailOP = STENCIL_OP_KEEP;
        enumStencilOpType eStencilPassOP = STENCIL_OP_KEEP;

        void Reset()
        {
            bStencilTestEnable = false;
            eStencilCompareOp = STENCIL_TEST_NEVER;
            nReference = 0;
            uMask = 0;
            eStencilFailOP = STENCIL_OP_KEEP;
            eStencilDepthFailOP = STENCIL_OP_KEEP;
            eStencilPassOP = STENCIL_OP_KEEP;
        }
    };

    ///////////////////////////////////// 这些都要删除
    ///

    enum class KRenderUsageType
    {
        ERROR_USAGE,
        MAIN_DEPTH_PREPASS,
        MAIN_CAMERA_RENDER,
        MAIN_CAMERA_OPAQUE_RENDER,
        MAIN_CAMERA_OPAQUE_NOSHADOW_RENDER,       // = MAIN_CAMERA_OPAQUE_RENDER + 1,
        MAIN_CAMERA_TERRAIN_CULL_RENDER,
        MAIN_CAMERA_TERRAIN_CULL_NOSHADOW_RENDER, // = MAIN_CAMERA_TERRAIN_CULL_RENDER + 1, // 用于阴影的占位
        MAIN_CAMERA_TRANSPARENT_RENDER,
        MAIN_CAMERA_TRANSPARENT_NOSHADOW_RENDER,  // = MAIN_CAMERA_TRANSPARENT_RENDER + 1,
        MAIN_CAMERA_MASK_RENDER,
        MAIN_CAMERA_MASK_NOSHADOW_RENDER,         // = MAIN_CAMERA_MASK_RENDER + 1,
        MAIN_CAMERA_OIT_OPAQUE_RENDER,
        MAIN_CAMERA_OIT_TRANSPARENT_RENDER,
        MAIN_CAMERA_SSS_DIFFUSE_RENDER,
        MAIN_CAMERA_SSS_BLUR_RENDER,
        MAIN_CAMERA_SSS_COMBINE_RENDER,
        MAIN_CAMERA_FORLIAGE_RENDER,
        MAIN_CAMERA_FORLIAGE_NOSHADOW_RENDER,           // = MAIN_CAMERA_FORLIAGE_RENDER + 1,
        MAIN_CAMERA_FORLIAGE_TRANSPARENT_RENDER,
        MAIN_CAMERA_FORLIAGE_BILLBOARD_RENDER,
        MAIN_CAMERA_FORLIAGE_BILLBOARD_NOSHADOW_RENDER, // = MAIN_CAMERA_FORLIAGE_BILLBOARD_RENDER + 1,
        MAIN_CAMERA_TERRAIN_LOW_BAKER_RENDER_CULLPASS0,
        MAIN_CAMERA_TERRAIN_HI_BAKER_FINAL_RENDER_CULLPASS0,
        MAIN_CAMERA_TERRAIN_LOW_BAKER_RENDER,
        MAIN_CAMERA_TERRAIN_HI_BAKER_FINAL_RENDER,
        MAIN_CAMERA_WATER_RENDER,
        MAIN_CAMERA_DEFERRED_RENDER_GBUFFER,
        MAIN_CAMERA_DEFERRED_LIGHTING,
        MAIN_CAMERA_POST_RENDER,
        SHADOWMAP_0_RENDER,
        SHADOWMAP_1_RENDER,
        SHADOWMAP_2_RENDER,
        SHADOWMAP_0_RENDER_MASK,
        SHADOWMAP_1_RENDER_MASK,
        SHADOWMAP_2_RENDER_MASK,
        SHADOWMAP_0_OIT,
        SHADOWMAP_1_OIT,
        SHADOWMAP_2_OIT,
        POST_RENDER,
        SKYBOX_RENDER,
        TERRAIN_SUM_ALPHA_BAKER_RENDER,
        TERRAIN_LAYER_2HLB_1WLB_BLEND_BAKER_RENDER,
        TERRAIN_LAYER_0ALB_BLEND_BAKER_RENDER,
        TERRAIN_COPY_BAKE_TO_CACHE_RENDER,
        TERRAIN_DEFER_PREZ,
        TERRAIN_DEFER_SUMALPHA,
        TERRAIN_DEFER_2HLB_1WLB,
        TERRAIN_DEFER_0ALB,
        TERRAIN_DEFER_SWAP_COPY,
        TERRAIN_DEFER_NORMAL_DEBUG,
        PSSR_RENDER,
        PSS_RENDER,
        PSS_UNDER_WATER,
        OCCLUSION_CULL_RENDER,
        MAIN_COLOR_DEPTH_COPY,
        MAIN_COLOR_DEPTH_SAVE,
        COLOR_COPY,
        STATIC_DEPTH_COPY,
        DEFERRED_LIGHTING_RENDER,
        SFX_RENDER,
        K3DUI_RENDER,
        OUTLINE_BACKGROUND_RENDER,
        OUTLINE_BACKGROUND_RENDER_NO_SHADOW = OUTLINE_BACKGROUND_RENDER + 1,
        OUTLINE_RESULT_RENDER,
        SPHERESHADOW_RENDER,
        POINT_CLOUD_RENDER,
        DECAL_LEGACY_RENDER,
        DECAL_SPHERE_RENDER,
        SPOT1_LIGHT_OPAQUE_MESH_RENDER,
        SPOT1_LIGHT_OIT_MESH_RENDER,
        SPOT2_LIGHT_OPAQUE_MESH_RENDER,
        SPOT2_LIGHT_OIT_MESH_RENDER,
        MAIN_CAMERA_OIT1_RENDER,
        WEATHER_TOP_CAMERA_DEPTH,
        WEATHER_TOP_CAMERA_TERRAIN_CULL,
        WEATHER_TOP_CAMERA_WATER_MODEL,
        WEATHER_TOP_CAMERA_WATER_RENDER,
        KRenderUsageType_Count
    };
    static const char* g_strRenderUsageName[] =
    {
        "ERROR_USAGE",
        "MAIN_DEPTH_PREPASS",
        "MAIN_CAMERA_RENDER",
        "MAIN_CAMERA_OPAQUE_RENDER",
        "MAIN_CAMERA_OPAQUE_NOSHADOW_RENDER",       // = MAIN_CAMERA_OPAQUE_RENDER + 1,
        "MAIN_CAMERA_TERRAIN_CULL_RENDER",
        "MAIN_CAMERA_TERRAIN_CULL_NOSHADOW_RENDER", // = MAIN_CAMERA_TERRAIN_CULL_RENDER + 1, // 用于阴影的占位
        "MAIN_CAMERA_TRANSPARENT_RENDER",
        "MAIN_CAMERA_TRANSPARENT_NOSHADOW_RENDER",  // = MAIN_CAMERA_TRANSPARENT_RENDER + 1,
        "MAIN_CAMERA_MASK_RENDER",
        "MAIN_CAMERA_MASK_NOSHADOW_RENDER",         // = MAIN_CAMERA_MASK_RENDER + 1,
        "MAIN_CAMERA_OIT_OPAQUE_RENDER",
        "MAIN_CAMERA_OIT_TRANSPARENT_RENDER",
        "MAIN_CAMERA_SSS_DIFFUSE_RENDER",
        "MAIN_CAMERA_SSS_BLUR_RENDER",
        "MAIN_CAMERA_SSS_COMBINE_RENDER",
        "MAIN_CAMERA_FORLIAGE_RENDER",
        "MAIN_CAMERA_FORLIAGE_TRANSPARENT_RENDER",
        "MAIN_CAMERA_FORLIAGE_NOSHADOW_RENDER",           // = MAIN_CAMERA_FORLIAGE_RENDER + 1,
        "MAIN_CAMERA_FORLIAGE_BILLBOARD_RENDER",
        "MAIN_CAMERA_FORLIAGE_BILLBOARD_NOSHADOW_RENDER", // = MAIN_CAMERA_FORLIAGE_BILLBOARD_RENDER + 1,
        "MAIN_CAMERA_TERRAIN_LOW_BAKER_RENDER_CULLPASS0",
        "MAIN_CAMERA_TERRAIN_HI_BAKER_FINAL_RENDER_CULLPASS0",
        "MAIN_CAMERA_TERRAIN_LOW_BAKER_RENDER",
        "MAIN_CAMERA_TERRAIN_HI_BAKER_FINAL_RENDER",
        "MAIN_CAMERA_WATER_RENDER",
        "MAIN_CAMERA_DEFERRED_RENDER_GBUFFER",
        "MAIN_CAMERA_DEFERRED_LIGHTING",
        "MAIN_CAMERA_POST_RENDER",
        "SHADOWMAP_0_RENDER",
        "SHADOWMAP_1_RENDER",
        "SHADOWMAP_2_RENDER",
        "SHADOWMAP_0_RENDER_MASK",
        "SHADOWMAP_1_RENDER_MASK",
        "SHADOWMAP_2_RENDER_MASK",
        "SHADOWMAP_0_OIT",
        "SHADOWMAP_1_OIT",
        "SHADOWMAP_2_OIT",
        "POST_RENDER",
        "SKYBOX_RENDER",
        "TERRAIN_SUM_ALPHA_BAKER_RENDER",
        "TERRAIN_LAYER_2HLB_1WLB_BLEND_BAKER_RENDER",
        "TERRAIN_LAYER_0ALB_BLEND_BAKER_RENDER",
        "TERRAIN_COPY_BAKE_TO_CACHE_RENDER",
        "TERRAIN_DEFER_PREZ",
        "TERRAIN_DEFER_SUMALPHA",
        "TERRAIN_DEFER_2HLB_1WLB",
        "TERRAIN_DEFER_0ALB",
        "TERRAIN_DEFER_SWAP_COPY",
        "TERRAIN_DEFER_NORMAL_DEBUG",
        "PSSR_RENDER",
        "PSS_RENDER",
        "PSS_UNDER_WATER",
        "OCCLUSION_CULL_RENDER",
        "MAIN_COLOR_DEPTH_COPY",
        "MAIN_COLOR_DEPTH_SAVE",
        "COLOR_COPY",
        "STATIC_DEPTH_COPY",
        "DEFERRED_LIGHTING_RENDER",
        "SFX_RENDER",
        "K3DUI_RENDER",
        "OUTLINE_BACKGROUND_RENDER",
        "OUTLINE_BACKGROUND_RENDER_NO_SHADOW",
        "OUTLINE_RESULT_RENDER",
        "SPHERESHADOW_RENDER",
        "POINT_CLOUD_RENDER",
        "DECAL_LEGACY_RENDER",
        "DECAL_SPHERE_RENDER",
        "SPOT1_LIGHT_OPAQUE_MESH_RENDER",
        "SPOT1_LIGHT_OIT_MESH_RENDER",
        "SPOT2_LIGHT_OPAQUE_MESH_RENDER",
        "SPOT2_LIGHT_OIT_MESH_RENDER",
        "PRE_OIT_RENDER",
        "WEATHER_TOP_CAMERA_DEPTH",
        "WEATHER_TOP_CAMERA_TERRAIN_CULL",
        "WEATHER_TOP_CAMERA_WATER_MODEL",
        "WEATHER_TOP_CAMERA_WATER_RENDER"
    };
    static_assert(
        sizeof(g_strRenderUsageName) / sizeof(g_strRenderUsageName[0]) == (uint32_t)KRenderUsageType::KRenderUsageType_Count,
        "KRenderUsageType g_strRenderUsageName must have same size"
        );
    inline BOOL IsTranslucent(const gfx::KRenderUsageType eRenderUsage)
    {
        switch (eRenderUsage)
        {
        case gfx::KRenderUsageType::MAIN_CAMERA_TRANSPARENT_RENDER:
        case gfx::KRenderUsageType::MAIN_CAMERA_TRANSPARENT_NOSHADOW_RENDER:
        case gfx::KRenderUsageType::MAIN_CAMERA_OIT_TRANSPARENT_RENDER:
        case gfx::KRenderUsageType::SFX_RENDER:
        case gfx::KRenderUsageType::MAIN_CAMERA_FORLIAGE_TRANSPARENT_RENDER:
        case gfx::KRenderUsageType::MAIN_CAMERA_OIT1_RENDER:
            return TRUE;
        default:
            return FALSE;
        }
    }

    //////////////////////////////////


    enum class KTerrainRenderOption
    {
        TERRAINCULL_PASS0,
        MAIN_PASS
    };

    enum class CommonParamUBOUsage : int
    {
        Main = 0,
        Shadow = 1,
        UI = 2,
    };

    struct RenderSystemInfo
    {
        std::string pAppName;
        BOOL        bCreateInstance = true;
        void* applicationVM = nullptr;
        void* applicationActivity = nullptr;
        BOOL        bByShaderBuilderCmdTools = false;
        BOOL        bMessageBox = true;
    };

    struct KIndexIndirectCommandData
    {
        uint32_t uIndexCount;
        uint32_t uInstanceCount;
        uint32_t uFirstIndex;
        int32_t  nVertexOffset;
        uint32_t uFirstInstance;
    };

    struct KIndirectCommandData
    {
        uint32_t vertexCount;
        uint32_t instanceCount;
        uint32_t firstVertex;
        uint32_t firstInstance;
    };

    struct KGfxCopyRegion
    {
        uint32_t left;
        uint32_t top;
        uint32_t front;
        uint32_t right;
        uint32_t bottom;
        uint32_t back;
    };

    struct KBufferCopyRegion
    {
        uint64_t uSrcOffset;
        uint64_t uDstOffset;
        uint64_t uSize;
    };

    struct KTextureCopyRegion
    {
        uint32_t srcMipLevel;
        uint32_t srcArraySlice;
        uint32_t srcLeft;
        uint32_t srcTop;
        uint32_t srcFront;

        uint32_t dstMipLevel;
        uint32_t dstArraySlice;
        uint32_t dstLeft;
        uint32_t dstTop;
        uint32_t dstFront;

        uint32_t extentWidth;
        uint32_t extentHeight;
        uint32_t extentDepth;
    };

    struct KBufferTextureCopy
    {
        uint32_t bufferOffset;      // 从缓冲区起始位置到复制数据起点的偏移量（以字节为单位）
        uint32_t bufferRowLength;   // 指定缓冲区中每一行像素的跨度（以像素为单位），用于控制内存布局。若为 0，则假设数据紧密排列（无额外填充）
        uint32_t bufferImageHeight; // 指定缓冲区中每个图像切片（image slice）的高度（以像素为单位），用于控制内存布局。若为 0，则假设数据紧密排列（无额外填充）

        uint32_t       textureMipLevel;
        uint32_t       textureArraySlice;
        KGfxCopyRegion textureCopyRegion;
    };

    enum enumVertexInputRate : uint8_t
    {
        VERTEX_INPUT_RATE_VERTEX,
        VERTEX_INPUT_RATE_INSTANCE
    };

    enum class MRTMask : char
    {
        None = 0,
        Normal = 0x1,
        MotionVector = 0x2,
        SunLight = 0x4,
        Albedo = 0x8,
    };

    enum enumTextureFormat : uint32_t
    {
        TEX_FORMAT_NONE = 0,

        TEX_FORMAT_R8G8B8A8_UNORM,
        TEX_FORMAT_R8G8B8A8_SNORM,
        TEX_FORMAT_R8G8B8A8_SRGB,
        TEX_FORMAT_R8G8B8A8_UINT,

        TEX_FORMAT_R8_UNORM,
        TEX_FORMAT_R8G8_UNORM,
        TEX_FORMAT_R16G16_UINT,
        TEX_FORMAT_B8G8R8_UNORM,
        TEX_FORMAT_R8G8B8_UNORM,

        TEX_FORMAT_B8G8R8A8_UNORM,
        TEX_FORMAT_B8G8R8A8_SRGB,
        TEX_FORMAT_R16G16B16A16_UNORM,

        TEX_FORMAT_R16G16B16A16_SFLOAT,
        TEX_FORMAT_R32G32B32A32_SFLOAT,
        TEX_FORMAT_R16_SFLOAT,
        TEX_FORMAT_R16_UINT,

        TEX_FORMAT_R16G16_SFLOAT,
        TEX_FORMAT_R32_SINT,
        TEX_FORMAT_R32_UINT,

        TEX_FORMAT_R32_FLOAT,
        TEX_FORMAT_D24_UNORM_S8_UINT,
        TEX_FORMAT_D16_UNORM,
        TEX_FORMAT_D32_SFLOAT,
        TEX_FORMAT_D32_SFLOAT_S8_UINT,

        TEX_FORMAT_R64_UINT,

        TEX_FORMAT_BC1_RGB_UNORM,
        TEX_FORMAT_BC1_RGBA_UNORM,
        TEX_FORMAT_BC2_UNORM,
        TEX_FORMAT_BC3_UNORM,

        TEX_FORMAT_BC4_UNORM,
        TEX_FORMAT_BC4_SNORM,
        TEX_FORMAT_BC5_UNORM,
        TEX_FORMAT_BC5_SNORM,
        TEX_FORMAT_BC6H_UFLOAT,
        TEX_FORMAT_BC6H_SFLOAT,
        TEX_FORMAT_BC7_UNORM,
        TEX_FORMAT_BC7_SRGB_UNORM,

        TEX_FORMAT_B5G6R5_UNORM_PACK16,
        TEX_FORMAT_A2R10G10B10_UNORM_PACK32,

        TEX_FORMAT_B10G11R11_UFLOAT_PACK32,
        TEX_FORMAT_ETC2_R8G8B8_UNORM_BLOCK,
        TEX_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK,
        TEX_FORMAT_ETC2_R_UNORM_BLOCK,
        TEX_FORMAT_ETC2_R_SNORM_BLOCK,
        TEX_FORMAT_ETC2_RG_UNORM_BLOCK,
        TEX_FORMAT_ETC2_RG_SNORM_BLOCK,

        TEX_FORMAT_ASTC_4X4_UNORM_BLOCK,
        TEX_FORMAT_ASTC_6X6_UNORM_BLOCK,
        TEX_FORMAT_ASTC_8X8_UNORM_BLOCK,
        TEX_FORMAT_R32G32_UINT,
        TEX_FORMAT_R32G32B32A32_UINT,

        TEX_FORMAT_R16G16_UNORM,

        TEX_FORMAT_R8_UINT,

        TEX_FORMAT_R16_UNORM,
        TEX_FORMAT_COUNT
    };

    enum enumSampleCountFlag : uint32_t
    {
        SAMPLE_COUNT_1_BIT = 1,
        SAMPLE_COUNT_2_BIT = 2,
        SAMPLE_COUNT_4_BIT = 3,
        SAMPLE_COUNT_8_BIT = 4,
        SAMPLE_COUNT_16_BIT = 5,
        SAMPLE_COUNT_32_BIT = 6,
        SAMPLE_COUNT_64_BIT = 7
    };

    enum enumPipelineStageFlag : uint32_t
    {
        PIPELINE_STAGE_TOP_OF_PIPE_BIT = 0x00000001,
        PIPELINE_STAGE_DRAW_INDIRECT_BIT = 0x00000002,
        PIPELINE_STAGE_VERTEX_INPUT_BIT = 0x00000004,
        PIPELINE_STAGE_VERTEX_SHADER_BIT = 0x00000008,
        PIPELINE_STAGE_TESSELLATION_CONTROL_SHADER_BIT = 0x00000010,
        PIPELINE_STAGE_TESSELLATION_EVALUATION_SHADER_BIT = 0x00000020,
        PIPELINE_STAGE_GEOMETRY_SHADER_BIT = 0x00000040,
        PIPELINE_STAGE_FRAGMENT_SHADER_BIT = 0x00000080,
        PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT = 0x00000100,
        PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT = 0x00000200,
        PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT = 0x00000400,
        PIPELINE_STAGE_COMPUTE_SHADER_BIT = 0x00000800,
        PIPELINE_STAGE_TRANSFER_BIT = 0x00001000,
        PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT = 0x00002000,
        PIPELINE_STAGE_HOST_BIT = 0x00004000,
        PIPELINE_STAGE_ALL_GRAPHICS_BIT = 0x00008000,
        PIPELINE_STAGE_ALL_COMMANDS_BIT = 0x00010000,
        PIPELINE_STAGE_TRANSFORM_FEEDBACK_BIT_EXT = 0x01000000,
        PIPELINE_STAGE_CONDITIONAL_RENDERING_BIT_EXT = 0x00040000,
        PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR = 0x00200000,
        PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR = 0x02000000,
        PIPELINE_STAGE_SHADING_RATE_IMAGE_BIT_NV = 0x00400000,
        PIPELINE_STAGE_TASK_SHADER_BIT_NV = 0x00080000,
        PIPELINE_STAGE_MESH_SHADER_BIT_NV = 0x00100000,
        PIPELINE_STAGE_FRAGMENT_DENSITY_PROCESS_BIT_EXT = 0x00800000,
        PIPELINE_STAGE_COMMAND_PREPROCESS_BIT_NV = 0x00020000,
        PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_NV = PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
        PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_NV = PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
        PIPELINE_STAGE_FLAG_BITS_MAX_ENUM = 0x7FFFFFFF,
    };

    enum enumDependencyFlag : uint32_t
    {
        DEPENDENCY_BY_REGION_BIT = 0x00000001,
        DEPENDENCY_DEVICE_GROUP_BIT = 0x00000004,
        DEPENDENCY_VIEW_LOCAL_BIT = 0x00000002,
        DEPENDENCY_VIEW_LOCAL_BIT_KHR = DEPENDENCY_VIEW_LOCAL_BIT,
        DEPENDENCY_DEVICE_GROUP_BIT_KHR = DEPENDENCY_DEVICE_GROUP_BIT,
        DEPENDENCY_FLAG_BITS_MAX_ENUM = 0x7FFFFFFF
    };

    enum ImageUsageFlagBits : uint32_t
    {
        IMAGE_USAGE_TRANSFER_SRC_BIT = 0x00000001,
        IMAGE_USAGE_TRANSFER_DST_BIT = 0x00000002,
        IMAGE_USAGE_SAMPLED_BIT = 0x00000004,
        IMAGE_USAGE_STORAGE_BIT = 0x00000008,
        IMAGE_USAGE_COLOR_ATTACHMENT_BIT = 0x00000010,
        IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT = 0x00000020,
        IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT = 0x00000040,
        IMAGE_USAGE_INPUT_ATTACHMENT_BIT = 0x00000080,
        IMAGE_USAGE_SHADING_RATE_IMAGE_BIT_NV = 0x00000100,
        IMAGE_USAGE_FRAGMENT_DENSITY_MAP_BIT_EXT = 0x00000200,
        IMAGE_USAGE_FLAG_BITS_MAX_ENUM = 0x7FFFFFFF
    };

    enum ImageAspectFlagBits : uint32_t
    {
        IMAGE_ASPECT_COLOR_BIT = 0x00000001,
        IMAGE_ASPECT_DEPTH_BIT = 0x00000002,
        IMAGE_ASPECT_STENCIL_BIT = 0x00000004,
        IMAGE_ASPECT_METADATA_BIT = 0x00000008,
        IMAGE_ASPECT_PLANE_0_BIT = 0x00000010,
        IMAGE_ASPECT_PLANE_1_BIT = 0x00000020,
        IMAGE_ASPECT_PLANE_2_BIT = 0x00000040,
        IMAGE_ASPECT_MEMORY_PLANE_0_BIT_EXT = 0x00000080,
        IMAGE_ASPECT_MEMORY_PLANE_1_BIT_EXT = 0x00000100,
        IMAGE_ASPECT_MEMORY_PLANE_2_BIT_EXT = 0x00000200,
        IMAGE_ASPECT_MEMORY_PLANE_3_BIT_EXT = 0x00000400,
        IMAGE_ASPECT_PLANE_0_BIT_KHR = IMAGE_ASPECT_PLANE_0_BIT,
        IMAGE_ASPECT_PLANE_1_BIT_KHR = IMAGE_ASPECT_PLANE_1_BIT,
        IMAGE_ASPECT_PLANE_2_BIT_KHR = IMAGE_ASPECT_PLANE_2_BIT,
        IMAGE_ASPECT_FLAG_BITS_MAX_ENUM = 0x7FFFFFFF
    };

    /// Data structure holding necessary info to create a Texture
    typedef struct KClearValue
    {
        // Anonymous structures generates warnings in C++11.
        // See discussion here for more info: https://stackoverflow.com/questions/2253878/why-does-c-disallow-anonymous-structs
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4201) // warning C4201: nonstandard extension used: nameless struct/union
#endif
        union {
            struct
            {
                float r;
                float g;
                float b;
                float a;
            };
            struct
            {
                float    depth;
                uint32_t stencil;
            };
            struct
            {
                uint32_t ux;
                uint32_t uy;
                uint32_t uz;
                uint32_t uw;
            };
        };
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
        KClearValue() : ux(0), uy(0), uz(0), uw(0) {}
        KClearValue(float InDepth, uint32_t InStencil) : depth(InDepth), stencil(InStencil) {}
        KClearValue(float InR, float InG, float InB, float InA) : r(InR), g(InG), b(InB), a(InA) {}
        KClearValue(float v) : r(v), g(v), b(v), a(v) {}
        KClearValue(uint32_t InX, uint32_t InY, uint32_t InZ, uint32_t InW) : ux(InX), uy(InY), uz(InZ), uw(InW) {}
        KClearValue(uint32_t v) : ux(v), uy(v), uz(v), uw(v) {}

        bool operator==(const KClearValue& other) const
        {
            return (ux == other.ux && uy == other.uy && uz == other.uz && uw == other.uw);
        }

        bool operator!=(const KClearValue& other) const
        {
            return !(*this == other);
        }

        void Zero()
        {
            ux = 0;
            uy = 0;
            uz = 0;
            uw = 0;
        }

        static KClearValue GetZero()
        {
            KClearValue v;
            v.Zero();
            return v;
        }
    } KClearValue;

    typedef struct KClearAttchment
    {
        uint32_t            uAttachmentIndex{ 0 };
        ImageAspectFlagBits eAspectMask;
        KClearValue         sClearValue;
    } KClearAttchment;

    struct KWindow
    {
        enumGraphicContext m_uId;
        char               m_szWindowName[128];

#ifdef _WIN32
        HINSTANCE m_connection = nullptr;
        HWND      m_window = nullptr;
#endif

#ifdef __ANDROID__
        ANativeWindow* m_window = nullptr;
#endif

#ifdef VK_USE_PLATFORM_WAYLAND_KHR
        wl_display* m_connection = nullptr;
        wl_surface* m_window = nullptr;
#endif

#ifdef VK_USE_PLATFORM_XCB_KHR
        xcb_connection_t* m_connection = nullptr;
        xcb_window_t      m_window = 0;
#endif

        // #if (defined(VK_USE_PLATFORM_IOS_MVK) || defined(VK_USE_PLATFORM_MACOS_MVK))
#ifdef __APPLE__
        void* m_window = nullptr;
#endif
#ifdef __OHOS__
        OHNativeWindow* m_window = nullptr;
#endif

#ifdef VK_USE_PLATFORM_XCB_KHR

#endif
        void SetWindow(void* pWindow)
        {
            if (m_window != pWindow)
            {
#ifdef _WIN32
                m_window = (HWND)pWindow;
#endif
#ifdef __ANDROID__
                m_window = (ANativeWindow*)pWindow;
#endif
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
                m_window = (wl_surface*)pWindow;
#endif
#ifdef VK_USE_PLATFORM_XCB_KHR
                m_window = (xcb_window_t*)pWindow
#endif
#ifdef __APPLE__
                    m_window = pWindow;
#endif
#ifdef __OHOS__
                m_window = (OHNativeWindow*)pWindow;
#endif
                m_bWindowInvalidated = true;
            }
        }
        BOOL     m_bWindowInvalidated = false;
        uint32_t m_uWidth = 0;
        uint32_t m_uHeight = 0;
        uint32_t m_uRenderWidth = 0;
        uint32_t m_uRenderHeight = 0;
        uint32_t m_uSwapChainWidth = 0;
        uint32_t m_uSwapChainHeight = 0;
        KWindow();
        void DestroyWindowA();
    };

    struct RenderSwitchList
    {
        bool bEnableDof = true;
        bool bLightShaftBloom = true;
        bool bLightOcclusion = true;
        bool bEnableVignette = true;
        // bool bEnableSSAO = true;
        // bool bEnableSSR = true;
        bool bHeightFog = true;
        bool bRenderShadow = true;
        bool bEnableCAS = true;

        void Reset()
        {
            bEnableDof = true;
            bLightShaftBloom = true;
            bLightOcclusion = true;
            bEnableVignette = true;
            bHeightFog = true;
            bRenderShadow = true;
            bEnableCAS = true;
        }
    };

    struct ViewportDescription
    {
        float TopLeftX = 0.0f; // 视口在帧缓冲中的左上角 X 坐标（以像素为单位）
        float TopLeftY = 0.0f; // 视口在帧缓冲中的左上角 Y 坐标（以像素为单位）
        float Width = 0.0f; // 视口的宽度（以像素为单位）
        float Height = 0.0f; // 视口的高度（以像素为单位）
        float MinDepth = 0.0f; // 深度值的最小范围（通常为 0.0f）
        float MaxDepth = 1.0f; // 深度值的最大范围（通常为 1.0f）

        void Reset()
        {
            TopLeftX = 0;
            TopLeftY = 0;
            Width = 0;
            Height = 0;
            MinDepth = 0.0f;
            MaxDepth = 1.0f;
        }
    };

    class IKSharedPreBinder;
    class IKGFX_Swapchain;

    struct RenderCommonParam
    {
        IKRender* piRender = nullptr;
        IKGFX_RenderContext* pRenderCtx = nullptr;
        NSEngine::KGlobalUBO* pGlobalUBO = nullptr;

        uint32_t            uSceneRenderID = 0;
        RenderSwitchList    SwitchList;
        ViewportDescription Viewport;
        uint32_t            nRenderRTWidth;  // 渲染RT的大小,上面的Viewport就是基于这个RT的
        uint32_t            nRenderRTHeight; // 渲染RT的大小,上面的Viewport就是基于这个RT的
        EnuRenderProcess    enuCurRenderProcess = EnuRenderProcess::UNDEFINED;
        CustomRenderState   customRenderState;
        BOOL                bCameraCaptureRender = FALSE;
        mutable uint64_t    uRenderNotReadySignal = 0; // 遇到一个自己+1；用法：记下当前值，调一个子函数后看值有没有增加，就知道子函数调用过程中是否有发生因

        NSRender::KRenderScene* pRenderScene{ nullptr };
        NSRender::KRenderSceneView* pRenderSceneView{ nullptr };
        NSRender::KRenderSceneCullResult* pCullResult{ nullptr };

        void Reset()
        {
            pRenderCtx = nullptr;
            uSceneRenderID = 0;
            SwitchList.Reset();
            Viewport.Reset();
            enuCurRenderProcess = EnuRenderProcess::UNDEFINED;
            customRenderState.Reset();
            bCameraCaptureRender = FALSE;
            uRenderNotReadySignal = 0;
            pRenderScene = nullptr;
            pRenderSceneView = nullptr;
            pCullResult = nullptr;
            pGlobalUBO = nullptr;
        }

        void CheckRenderReady(bool bReady) const
        {
            if (!bReady)
                uRenderNotReadySignal++;
        }
        bool IsValid() const
        {
            return (pRenderCtx && pRenderScene && pCullResult && pRenderSceneView);
        }
    };

    enum class KGfxAccess : uint32_t
    {
        Unknown = 0,

        // Read states
        CPURead = 1 << 0,
        Present = 1 << 1,
        IndirectArgs = 1 << 2,
        VertexBuffer = 1 << 3,
        IndexBuffer = 1 << 4,
        ConstBuffer = 1 << 5,
        SRVCompute = 1 << 6,
        SRVGraphicsPixel = 1 << 7,
        SRVGraphicsNonPixel = 1 << 8,
        CopySrc = 1 << 9,
        ResolveSrc = 1 << 10,
        DSVRead = 1 << 11,

        // Read-write states
        UAVCompute = 1 << 12,
        UAVGraphics = 1 << 13,
        RTV = 1 << 14,
        CopyDst = 1 << 15,
        ResolveDst = 1 << 16,
        DSVWrite = 1 << 17,

        // Ray tracing acceleration structure states.
        // Buffer that contains an AS must always be in either of these states.
        // BVHRead -- required for AS inputs to build/update/copy/trace commands.
        // BVHWrite -- required for AS outputs of build/update/copy commands.
        BVHRead = 1 << 18,
        BVHWrite = 1 << 19,

        // Invalid released state (transient resources)
        Discard = 1 << 20,

        // Shading Rate Source
        ShadingRateSource = 1 << 21,

        // Shader Binding Table Read
        SBTRead = 1 << 22,

        Last = ShadingRateSource,
        None = Unknown,
        Mask = (Last << 1) - 1,

        // Graphics is a combination of pixel and non-pixel
        SRVGraphics = SRVGraphicsPixel | SRVGraphicsNonPixel,

        // A mask of the two possible SRV states
        SRVMask = SRVCompute | SRVGraphics,

        // A mask of the two possible UAV states
        UAVMask = UAVCompute | UAVGraphics,
    };
    ENUM_CLASS_OPERATORS(KGfxAccess);

    struct KGfxSubresourceRange
    {
        enum EType : uint32_t
        {
            s_uDepthAspect = 0,
            s_uStencilAspect = 1,
            s_All = UINT32_MAX,
        };

        uint32_t uBaseMipLevel = 0;
        uint32_t uBaseArraySlice = 0;
        uint32_t uMipCount = s_All;
        uint32_t uArrayCount = s_All;

        KGfxSubresourceRange() = default;

        KGfxSubresourceRange(
            uint32_t _uMipIndex,
            uint32_t _uArraySlice,
            uint32_t _uMipCount,
            uint32_t _uArrayCount
        )
            : uBaseMipLevel(_uMipIndex)
            , uBaseArraySlice(_uArraySlice)
            , uMipCount(_uMipCount)
            , uArrayCount(_uArrayCount)
        {
        }

        inline bool IsWholeResource() const
        {
            return uBaseMipLevel == 0 && uBaseArraySlice == 0 && uMipCount == s_All && uArrayCount == s_All;
        }

        inline bool operator==(KGfxSubresourceRange const& RHS) const
        {
            return uBaseMipLevel == RHS.uBaseMipLevel && uBaseArraySlice == RHS.uBaseArraySlice && uMipCount == RHS.uMipCount && uArrayCount == RHS.uArrayCount;
        }

        inline bool operator!=(KGfxSubresourceRange const& RHS) const
        {
            return !(*this == RHS);
        }
    };

    struct KGfxBarrier : public KGfxSubresourceRange
    {
        union {
            class IKGFX_Resource* pResource = nullptr;
            class IKGFX_TextureResource* pTexture;
            class IKGFX_TextureView* pTextureView;
            class IKGFX_Buffer* pBuffer;
            class KRenderTarget* pRenderTarget;
        };

        enum class EType : uint8_t
        {
            Unknown,
            Texture,
            TextureView,
            Buffer,
            RenderTarget,
        } eType = EType::Unknown;

        KGfxAccess eSRCAccess = KGfxAccess::Unknown;
        KGfxAccess eDSTAccess = KGfxAccess::Unknown;

        KGfxBarrier() = default;

        KGfxBarrier(
            class IKGFX_TextureResource* InTexture,
            KGfxAccess                   InPreviousState,
            KGfxAccess                   InNewState
        )
            : KGfxSubresourceRange()
            , pTexture(InTexture)
            , eType(EType::Texture)
            , eSRCAccess(InPreviousState)
            , eDSTAccess(InNewState)
        {
            assert(InTexture);
        }

        KGfxBarrier(
            class IKGFX_TextureResource* InTexture,
            KGfxAccess                   InPreviousState,
            KGfxAccess                   InNewState,
            uint32_t                     InBaseMipIndex,
            uint32_t                     InBaseArraySlice,
            uint32_t                     InMipCount,
            uint32_t                     InArrayCount
        )
            : KGfxSubresourceRange(InBaseMipIndex, InBaseArraySlice, InMipCount, InArrayCount)
            , pTexture(InTexture)
            , eType(EType::Texture)
            , eSRCAccess(InPreviousState)
            , eDSTAccess(InNewState)
        {
            assert(InTexture);
        }

        KGfxBarrier(
            class IKGFX_TextureResource* InTexture,
            KGfxAccess                   InPreviousState,
            KGfxAccess                   InNewState,
            const KGfxSubresourceRange& range
        )
            : KGfxSubresourceRange(range)
            , pTexture(InTexture)
            , eType(EType::Texture)
            , eSRCAccess(InPreviousState)
            , eDSTAccess(InNewState)
        {
            assert(InTexture);
        }

        KGfxBarrier(
            class IKGFX_TextureView* InTextureView,
            KGfxAccess               InPreviousState,
            KGfxAccess               InNewState
        )
            : KGfxSubresourceRange()
            , pTextureView(InTextureView)
            , eType(EType::TextureView)
            , eSRCAccess(InPreviousState)
            , eDSTAccess(InNewState)
        {
            assert(InTextureView);
        }

        KGfxBarrier(
            class KRenderTarget* InRT,
            KGfxAccess           InPreviousState,
            KGfxAccess           InNewState
        )
            : KGfxSubresourceRange()
            , pRenderTarget(InRT)
            , eType(EType::RenderTarget)
            , eSRCAccess(InPreviousState)
            , eDSTAccess(InNewState)
        {
            assert(InRT);
        }

        KGfxBarrier(
            class KRenderTarget* InRT,
            KGfxAccess           InPreviousState,
            KGfxAccess           InNewState,
            uint32_t             InBaseMipIndex,
            uint32_t             InBaseArraySlice,
            uint32_t             InMipCount,
            uint32_t             InArrayCount
        )
            : KGfxSubresourceRange(InBaseMipIndex, InBaseArraySlice, InMipCount, InArrayCount)
            , pRenderTarget(InRT)
            , eType(EType::RenderTarget)
            , eSRCAccess(InPreviousState)
            , eDSTAccess(InNewState)
        {
            assert(InRT);
        }

        KGfxBarrier(class IKGFX_Buffer* InRHIBuffer, KGfxAccess InPreviousState, KGfxAccess InNewState)
            : pBuffer(InRHIBuffer)
            , eType(EType::Buffer)
            , eSRCAccess(InPreviousState)
            , eDSTAccess(InNewState)
        {
            assert(InRHIBuffer);
        }

        inline bool operator==(KGfxBarrier const& RHS) const
        {
            return pResource == RHS.pResource && eType == RHS.eType && eSRCAccess == RHS.eSRCAccess && eDSTAccess == RHS.eDSTAccess && KGfxSubresourceRange::operator==(RHS);
        }

        inline bool operator!=(KGfxBarrier const& RHS) const
        {
            return !(*this == RHS);
        }
    };



    class KGfxRef
    {
    protected:
        std::atomic<int32_t> m_nRef{ 1 };

    public:
        KGfxRef()
        {
#if DESCRIPTORSET_VALIDATE
            m_uCreateId = RefSequenceCounter++;
#endif
        }
        virtual ~KGfxRef() = default;
        virtual int32_t AddRef() { return ++m_nRef; }
        virtual int32_t GetRef() { return m_nRef; }
        virtual int32_t Release()
        {
            int32_t nRef = --m_nRef;
            assert(nRef >= 0);
            if (nRef == 0)
            {
                Uninit();
                delete (this);
            }
            return nRef;
        }
        virtual void Uninit() {};

#if DESCRIPTORSET_VALIDATE
        uint32_t GetCreateId()
        {
            return m_uCreateId;
        }
        static std::atomic<uint32_t> RefSequenceCounter;
    private:
        uint32_t m_uCreateId = 0;
#endif
    };

    class KGFX_DelayReleaseObject
    {
    public:
        virtual ~KGFX_DelayReleaseObject() {};
    };

    class IKGFX_BufferView : public KGfxRef
    {
    public:
        ~IKGFX_BufferView() override = default;
        virtual IKGFX_Buffer* GetResource() = 0;
        virtual const KGFX_BufferViewDesc* GetViewDesc() const = 0;
        virtual void* GetViewHandle() = 0;
        virtual uintptr_t                  GetNativeHandle() = 0;
        virtual uint64_t                   GetCode() = 0;
        virtual void                       SetObjectName(const char* pcszName) = 0;
        virtual uint32_t                   GetBindlessHandle() = 0;
    };

    typedef struct KBlitRegion
    {
        uint32_t uX;
        uint32_t uY;
        uint32_t uWidth;
        uint32_t uHeight;
    } KBlitRegion;

    enum enumSamplerFilter : uint32_t
    {
        FILTER_NEAREST,
        FILTER_LINEAR,
        FILTER_CUBIC_IMG
    };

    enum enumMipMapMode : uint32_t
    {
        SAMPLER_MIPMAP_MODE_NEAREST,
        SAMPLER_MIPMAP_MODE_LINEAR
    };

    enum enumSamplerAddressMode : uint32_t
    {
        SAMPLER_ADDRESS_MODE_REPEAT,
        SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT,
        SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
        SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE,
    };

    enum enumSamplerCompareFunc : uint32_t
    {
        SAMPLER_COMPARE_OP_NEVER = 0,
        SAMPLER_COMPARE_OP_LESS = 1,
        SAMPLER_COMPARE_OP_EQUAL = 2,
        SAMPLER_COMPARE_OP_LESS_OR_EQUAL = 3,
        SAMPLER_COMPARE_OP_GREATER = 4,
        SAMPLER_COMPARE_OP_NOT_EQUAL = 5,
        SAMPLER_COMPARE_OP_GREATER_OR_EQUAL = 6,
        SAMPLER_COMPARE_OP_ALWAYS = 7
    };

    enum enumBorderColor : uint32_t
    {
        BORDER_COLOR_FLOAT_TRANSPARENT_BLACK,
        BORDER_COLOR_INT_TRANSPARENT_BLACK,
        BORDER_COLOR_FLOAT_OPAQUE_BLACK,
        BORDER_COLOR_INT_OPAQUE_BLACK,
        BORDER_COLOR_FLOAT_OPAQUE_WHITE,
        BORDER_COLOR_INT_OPAQUE_WHITE
    };

    enum enumTextureReductionOp : uint32_t
    {
        Average,
        Comparison,
        Minimum,
        Maximum,
    };

    struct KSamplerState
    {
        float fMipLodBias;
        float finialMipBias;
        float fMaxAnisotropy;
        float fToMinLod;
        float fToMaxLod;        
        union {
            struct
            {
                uint32_t               bCompareEnable : 1;
                enumSamplerCompareFunc enuCompareFunc : 3;
                uint32_t               bEnableMipmap : 1;
                enumSamplerFilter      enuMagFilter : 2;
                enumSamplerFilter      enuMinFilter : 2;
                enumMipMapMode         enuMipmapMode : 1;
                enumSamplerAddressMode enuAddressModeU : 3;
                enumSamplerAddressMode enuAddressModeV : 3;
                enumSamplerAddressMode enuAddressModeW : 3;
                enumBorderColor        enuBorderColor : 3;
                enumTextureReductionOp enuTextureReductionOp : 2;
            };
            uint32_t u;
        };

        BOOL  bNeedShaderInit;
        KSamplerState();
        KSamplerState(enumSamplerFilter minFilter, enumSamplerFilter magFilter, enumMipMapMode mipMapMode, enumSamplerAddressMode enuAddressModeU, enumSamplerAddressMode enuAddressModeV, enumSamplerAddressMode enuAddressModeW, float maxAnisotropy, enumBorderColor borderColor, enumTextureReductionOp textureReductionOp, enumSamplerCompareFunc func = SAMPLER_COMPARE_OP_NEVER);
        bool operator==(const KSamplerState& other) const;
        bool operator<(const KSamplerState& other) const;


        // fbxtool should use move from cpp to h

        const_pool_str GetKey();

    private:
        float m_fMipLodBias = 0.0f;
        float m_finialMipBias = 0.0f;
        float m_fMaxAnisotropy = 0.0f;
        float m_fToMinLod = 0.0f;
        float m_fToMaxLod = 0.0f;
        uint32_t m_u = 0u;
        const_pool_str m_pKey = nullptr;        
    };

    enum class TextureDimensionType : uint32_t
    {
        Texture1D,
        Texture2D,
        TextureCube,
        Texture3D,
    };

    // todo: TextureUsageFlagBits is same as VkBufferUsageFlagBits temporary.
    enum TextureUsageFlagBits : uint32_t
    {
        TEXTURE_USAGE_TRANSFER_SRC_BIT = 0x00000001,
        TEXTURE_USAGE_TRANSFER_DST_BIT = 0x00000002,
        TEXTURE_USAGE_SAMPLED_BIT = 0x00000004,
        TEXTURE_USAGE_STORAGE_BIT = 0x00000008,
        TEXTURE_USAGE_COLOR_ATTACHMENT_BIT = 0x00000010,
        TEXTURE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT = 0x00000020,
        TEXTURE_USAGE_TRANSIENT_ATTACHMENT_BIT = 0x00000040,
        TEXTURE_USAGE_INPUT_ATTACHMENT_BIT = 0x00000080,
        TEXTURE_USAGE_FLAG_BITS_MAX_ENUM = 0x7FFFFFFF
    };
    typedef uint32_t TextureUsageFlags;

    enum TextureAspectFlagBits : uint32_t
    {
        TEXTURE_ASPECT_COLOR_BIT = 0x00000001,
        TEXTURE_ASPECT_DEPTH_BIT = 0x00000002,
        TEXTURE_ASPECT_STENCIL_BIT = 0x00000004,
        TEXTURE_ASPECT_METADATA_BIT = 0x00000008,
        TEXTURE_ASPECT_FLAG_BITS_MAX_ENUM = 0x7FFFFFFF
    };


    typedef uint32_t TextureAspectFlags;
#pragma region DX12

    enum class KGFX_ClearResourceViewFlags : uint32_t
    {
        None = 0,
        ClearDepth = 1,
        ClearStencil = 2,
        FloatClearValues = 4,
        ClearDepthStencil = ClearDepth | ClearStencil,
    };

    RHI_ENUM_CLASS_OPERATORS(KGFX_ClearResourceViewFlags);

    enum class ShaderStageType : uint32_t
    {
        Vertex = 0x00000001,
        Hull = 0x00000002, // vk对应的是 TESSELLATION_CONTROL
        Domain = 0x00000004, // vk对应的是 TESSELLATION_EVALUATION
        Geometry = 0x00000008,
        Fragment = 0x00000010,
        Compute = 0x00000020,
        RayGeneration = 0x00000040,
        Intersection = 0x00000080,
        AnyHit = 0x00000100,
        ClosestHit = 0x00000200,
        Miss = 0x00000400,
        Callable = 0x00000800,
        Amplification = 0x00001000,
        Mesh = 0x00002000,
        CountOf = 0x00004000,
        HitGroup = AnyHit | ClosestHit | Intersection,
        AllGraphics = Vertex | Hull | Domain | Geometry | Fragment,
        ALLStages = AllGraphics | Compute | RayGeneration | Intersection | AnyHit | ClosestHit | Miss | Callable | Amplification | Mesh,
    };

    RHI_ENUM_CLASS_OPERATORS(ShaderStageType);


#define SHADER_GRAPHIC_AND_COMPUTE_STAGE_COUNT 6
    inline uint32_t GetGraphicAndComputeShaderId(gfx::ShaderStageType eShaderStage)
    {
        uint32_t id = 0;
        switch (eShaderStage)
        {
        case gfx::ShaderStageType::Vertex:
            id = 0;
            break;
        case gfx::ShaderStageType::Hull:
            id = 1;
            break;
        case gfx::ShaderStageType::Domain:
            id = 2;
            break;
        case gfx::ShaderStageType::Geometry:
            id = 3;
            break;
        case gfx::ShaderStageType::Fragment:
            id = 4;
            break;
        case gfx::ShaderStageType::Compute:
            id = 5;
            break;
        default:
            break;
        }
        return id;
    }

    RENDER_GRAPH_ENUM_INFO(
        ShaderStageType,
        {
            {ShaderStageType::Vertex,        "vs"           },
            {ShaderStageType::Hull,          "hs"           },
            {ShaderStageType::Domain,        "ds"           },
            {ShaderStageType::Geometry,      "gs"           },
            {ShaderStageType::Fragment,      "ps"           },
            {ShaderStageType::Compute,       "cs"           },
            {ShaderStageType::RayGeneration, "lib"},
            {ShaderStageType::Intersection,  "lib" },
            {ShaderStageType::AnyHit,        "lib"       },
            {ShaderStageType::ClosestHit,    "lib"   },
            {ShaderStageType::Miss,          "lib"         },
            {ShaderStageType::Callable,      "lib"     },
            {ShaderStageType::HitGroup,       "lib"},
            {ShaderStageType::Amplification, "Amplification"},
            {ShaderStageType::Mesh,          "ms"           }
        }
    );

    enum class ProfileVersion : uint32_t
    {
        Unknown,
        _5_0,
        _5_1,
        _6_0,
        _6_1,
        _6_2,
        _6_3,
        _6_4,
        _6_5,
        _6_6,
        _6_7,
        _6_8
    };

    RENDER_GRAPH_ENUM_INFO(
        ProfileVersion,
        {
            {ProfileVersion::Unknown, "Unknown"},
            {ProfileVersion::_5_0,    "_5_0"   },
            {ProfileVersion::_5_1,    "_5_1"   },
            {ProfileVersion::_6_0,    "_6_0"   },
            {ProfileVersion::_6_1,    "_6_1"   },
            {ProfileVersion::_6_2,    "_6_2"   },
            {ProfileVersion::_6_3,    "_6_3"   },
            {ProfileVersion::_6_4,    "_6_4"   },
            {ProfileVersion::_6_5,    "_6_5"   },
            {ProfileVersion::_6_6,    "_6_6"   },
            {ProfileVersion::_6_7,    "_6_7"   },
            {ProfileVersion::_6_8,    "_6_8"   }
        }
    );

    constexpr uint32_t CalculateShaderStageTypeCount()
    {
        uint32_t count = 0;
        for (uint32_t value = static_cast<uint32_t>(ShaderStageType::Vertex);
            value < static_cast<uint32_t>(ShaderStageType::CountOf);
            value <<= 1)
        {
            ++count;
        }
        return count;
    }

    constexpr uint32_t CalculateGraphicsShaderStageTypeCount()
    {
        uint32_t count = 0;
        for (uint32_t value = static_cast<uint32_t>(ShaderStageType::Vertex);
            value < static_cast<uint32_t>(ShaderStageType::Compute);
            value <<= 1)
        {
            ++count;
        }
        return count;
    }

    constexpr uint32_t CalculateGraphicsAndComputeShaderStageTypeCount()
    {
        uint32_t count = 0;
        for (uint32_t value = static_cast<uint32_t>(ShaderStageType::Vertex);
            value < static_cast<uint32_t>(ShaderStageType::RayGeneration);
            value <<= 1)
        {
            ++count;
        }
        return count;
    }

    inline uint32_t CalculateShaderStageTypeIndex(const ShaderStageType& other)
    {
        assert(other != ShaderStageType::AllGraphics);
        uint32_t count = 0;
        for (uint32_t value = static_cast<uint32_t>(ShaderStageType::Vertex);
            value < static_cast<uint32_t>(other);
            value <<= 1)
        {
            ++count;
        }
        return count;
    }

    constexpr uint16_t INVALID_USLOT = 0xffff;

    struct ShaderOffset
    {
        uint16_t bindingSpaceIndex = INVALID_USLOT;
        uint16_t bindingSlotIndex = INVALID_USLOT;
        uint32_t getHashCode() const
        {
            return (uint32_t)(((bindingSpaceIndex << 20) + bindingSlotIndex));
        }
        bool operator==(const ShaderOffset& other) const
        {
            return bindingSpaceIndex == other.bindingSpaceIndex &&
                bindingSlotIndex == other.bindingSlotIndex;
        }
        bool operator!=(const ShaderOffset& other) const { return !this->operator==(other); }
        bool operator<(const ShaderOffset& other) const
        {
            if (bindingSpaceIndex != other.bindingSpaceIndex)
            {
                return bindingSpaceIndex < other.bindingSpaceIndex;
            }
            if (bindingSlotIndex != other.bindingSlotIndex)
            {
                return bindingSlotIndex < other.bindingSlotIndex;
            }
            return false;
        }
        bool operator<=(const ShaderOffset& other) const { return (*this == other) || (*this) < other; }
        bool operator>(const ShaderOffset& other) const { return other < *this; }
        bool operator>=(const ShaderOffset& other) const { return other <= *this; }
        bool IsValid() const
        {
            return bindingSpaceIndex != INVALID_USLOT && bindingSlotIndex != INVALID_USLOT;
        }
    };
    constexpr int16_t INVALID_SLOT = -1;

    enum class KGfxResourceViewType : uint8_t
    {
        RESOURCE_VIEW_TYPE_UNKNOWN,
        RESOURCE_VIEW_TYPE_CBV,
        RESOURCE_VIEW_TYPE_SRV,
        RESOURCE_VIEW_TYPE_RTV,
        RESOURCE_VIEW_TYPE_DSV,
        RESOURCE_VIEW_TYPE_UAV,
        RESOURCE_VIEW_TYPE_SAMPLER,
    };

    struct ShaderResourceVisible
    {
        ShaderStageType      stageType = ShaderStageType::CountOf;
        KGfxResourceViewType slotType = KGfxResourceViewType::RESOURCE_VIEW_TYPE_UNKNOWN;
        ShaderOffset         slot;
        bool                 operator<(const ShaderResourceVisible& other) const
        {
            if (slotType != other.slotType)
                return slotType < other.slotType;
            if (stageType != other.stageType)
                return stageType < other.stageType;
            return slot < other.slot;
        }

        bool operator==(const ShaderResourceVisible& other) const noexcept
        {
            return stageType == other.stageType
                && slotType == other.slotType
                && slot == other.slot;
        }

        bool operator!=(const ShaderResourceVisible& other) const noexcept
        {
            return !(*this == other);
        }
    };

    struct FormatInfo
    {
        int      channelCount = 1; ///< The amount of channels in the format. Only set if the channelType is set
        // uint8_t channelType; ///< One of SlangScalarType None if type isn't made up of elements of type.
        uint32_t blockSizeInBytes = 4; ///< The size of a block in bytes.
        int      pixelsPerBlock = 1; ///< The number of pixels contained in a block.
        int      blockWidth = 1; ///< The width of a block in pixels.
        int      blockHeight = 1; ///< The height of a block in pixels.
    };

    enum class ResourceViewDimension : uint8_t
    {
        RESOURCE_DIMENSION_UNKNOWN,
        RESOURCE_DIMENSION_BUFFER,
        RESOURCE_DIMENSION_TEXTURE1D,
        RESOURCE_DIMENSION_TEXTURE1D_ARRAY,
        RESOURCE_DIMENSION_TEXTURE2D,
        RESOURCE_DIMENSION_TEXTURE2D_ARRAY,
        RESOURCE_DIMENSION_TEXTURECUBE,
        RESOURCE_DIMENSION_TEXTURECUBE_ARRAY,
        RESOURCE_DIMENSION_TEXTURE3D
    };

    enum BufferResourceViewFlagBit : uint32_t
    {
        BUFFER_RESOURCE_VIEW_FLAG_NONE = 0,
        BUFFER_RESOURCE_VIEW_FLAG_RAW = 1,
    };
    typedef uint32_t BufferResourceViewFlagBits;

    struct KGFX_BufferViewDesc
    {
        KGfxResourceViewType       eViewType = KGfxResourceViewType::RESOURCE_VIEW_TYPE_UNKNOWN;
        enumTextureFormat          eFormat;
        uint32_t                   uBytesOffset;     // in BYTEs
        uint32_t                   uBytesRange;      // in BYTEs
        uint32_t                   uStructureStride; // in BYTEs
        BufferResourceViewFlagBits Flags;

        bool operator==(const KGFX_BufferViewDesc& other) const
        {
            return eViewType == other.eViewType && eFormat == other.eFormat &&
                uBytesOffset == other.uBytesOffset && uBytesRange == other.uBytesRange &&
                uStructureStride == other.uStructureStride && Flags == other.Flags;
        }
        static KGFX_BufferViewDesc GetViewDesc_FullView(enumTextureFormat eFmt)
        {
            KGFX_BufferViewDesc viewDesc;
            viewDesc.eFormat = eFmt;
            viewDesc.uBytesOffset = 0;
            viewDesc.uBytesRange = 0;
            viewDesc.uStructureStride = 0;
            viewDesc.Flags = BUFFER_RESOURCE_VIEW_FLAG_NONE;
            return viewDesc;
        }
        static KGFX_BufferViewDesc GetViewDesc_StructuredBuffer(uint32_t uStructureStride)
        {
            KGFX_BufferViewDesc viewDesc;
            viewDesc.eFormat = TEX_FORMAT_NONE;
            viewDesc.uBytesOffset = 0;
            viewDesc.uBytesRange = 0;
            viewDesc.uStructureStride = uStructureStride;
            viewDesc.Flags = BUFFER_RESOURCE_VIEW_FLAG_NONE;
            return viewDesc;
        }
        static KGFX_BufferViewDesc GetViewDesc_RawBuffer()
        {
            KGFX_BufferViewDesc viewDesc;
            viewDesc.eFormat = TEX_FORMAT_NONE;
            viewDesc.uBytesOffset = 0;
            viewDesc.uBytesRange = 0;
            viewDesc.uStructureStride = 0;
            viewDesc.Flags = BUFFER_RESOURCE_VIEW_FLAG_RAW;
            return viewDesc;
        }
    };

    struct KGFX_TextureViewDesc
    {
        KGfxResourceViewType  eViewType = KGfxResourceViewType::RESOURCE_VIEW_TYPE_UNKNOWN;
        enumTextureFormat     eFormat = TEX_FORMAT_NONE;
        ResourceViewDimension eViewDimension = ResourceViewDimension::RESOURCE_DIMENSION_TEXTURE2D;
        TextureAspectFlagBits uAspectFlags = TextureAspectFlagBits::TEXTURE_ASPECT_FLAG_BITS_MAX_ENUM; /// 这个参数暂时不用设置，用默认值
        KGfxSubresourceRange  sSubresourceRange;

        bool operator==(const KGFX_TextureViewDesc& other) const
        {
            return eFormat == other.eFormat && uAspectFlags == other.uAspectFlags && eViewDimension == other.eViewDimension &&
                sSubresourceRange == other.sSubresourceRange && eViewType == other.eViewType;
        }
    };

    enum class KGfxResourceAccessType
    {
        KGfxResourceAccess_GPUOnly,
        KGfxResourceAccess_Read,
        KGfxResourceAccess_Write,
    };

    enum class KGfxResourceMiscFlag :uint32_t
    {
        AutoLayoutTransition = 0x00000001, ///< 自动转换Layout
    };
    using KGfxResourceMiscFlags = uint32_t;
    RHI_ENUM_CLASS_OPERATORS(KGfxResourceMiscFlag);

    struct KGFX_TextureDesc
    {
        static KGFX_TextureDesc g_EmptryValue;
        TextureDimensionType   eDimension = TextureDimensionType::Texture2D;
        KGfxResourceAccessType memoryType = KGfxResourceAccessType::KGfxResourceAccess_GPUOnly;
        TextureUsageFlags      uUsageFlags = 0;
        uint32_t               uWidth = 0;
        uint32_t               uHeight = 0;
        uint32_t               uDepth = 1;
        uint32_t               uArraySize = 1;
        uint32_t               uMipLevels = 1; // 设定当前Texture Mipmap层数，若为0则根据Texture尺寸自动计算全Mipmap数量
        enumTextureFormat      eFormat = enumTextureFormat::TEX_FORMAT_NONE;
        bool                   bSRGB = false;       
        enumSampleCountFlag    eSampleCount = SAMPLE_COUNT_1_BIT;
        uint32_t               sampleQuality = 0;
        KGfxResourceMiscFlags  eMiscFlags = 1; ///< 额外的标志位，暂时只支持 AutoLayoutTransition

        bool IsAutoRes() const
        {
            return is_set(static_cast<KGfxResourceMiscFlag>(eMiscFlags), KGfxResourceMiscFlag::AutoLayoutTransition);
        }

        bool IsCubeTex() const
        {
            return (eDimension == TextureDimensionType::TextureCube);
        }

        uint32_t GetSubResCount() const
        {
            assert(uArraySize > 0);
            assert(uMipLevels > 0);
            assert(uArraySize < 100);
            assert(uMipLevels < 100);
            return uArraySize * uMipLevels;
        }

        bool operator==(const KGFX_TextureDesc& other) const
        {
            return eDimension == other.eDimension &&
                memoryType == other.memoryType &&
                uWidth == other.uWidth &&
                uHeight == other.uHeight &&
                uDepth == other.uDepth &&
                uArraySize == other.uArraySize &&
                uMipLevels == other.uMipLevels &&
                eFormat == other.eFormat &&
                bSRGB == other.bSRGB &&
                eSampleCount == other.eSampleCount &&
                sampleQuality == other.sampleQuality;
        }
    };


    typedef struct KRenderTargetDesc : public KGFX_TextureDesc
    {
        const void* cpNativeHandle = nullptr;
        char m_szRTName[MAX_PATH] = "";
    } KRenderTargetDesc;


    class IKGFX_TextureView : public KGfxRef
    {
    public:
        virtual IKGFX_TextureResource* GetResource() const = 0;

        virtual const KGFX_TextureViewDesc& GetViewDesc() const = 0;

        /// Returns a native API handle representing this resource view object.
        /// When using D3D12, this will be a D3D12_CPU_DESCRIPTOR_HANDLE or a buffer device address
        /// depending on the type of the resource view. When using Vulkan, this will be a VkImageView,
        /// VkBufferView, VkAccelerationStructure or a VkBuffer depending on the type of the resource
        /// view.
        /// if the view type is 
        virtual uintptr_t GetNativeHandle() = 0;

        /// <summary>
        /// return a Bindless Id refers to the index of the resource view in the global bindless heap
        /// valid use : The view type is bindless srv/bindless uav, or will be asserted and return UINT32_MAX;
        /// </summary>
        /// <returns>Bindless Id</returns>
        virtual uint32_t GetBindlessHandle() = 0;
        virtual void SetDebugName(const char* szDebugName) = 0;
    };

    struct KGfxFrameBufferTargetDesc
    {
        IKGFX_TextureView* pTargetView = nullptr;
        KClearValue ClearValue = {};
        enumLoadActionType eLoadActions = LOAD_ACTION_LOAD;
        enumLoadActionType eStencilLoadActions = LOAD_ACTION_LOAD;

        KGfxFrameBufferTargetDesc()
        {
        }

        inline void SetTarget(IKGFX_TextureView* pInView)
        {
            pTargetView = pInView;
        }

        inline void ClearRTV(const gfx::KClearValue& InClearValue)
        {
            ClearValue = InClearValue;
            eLoadActions = LOAD_ACTION_CLEAR;
        }

        inline void ClearDSV(const gfx::KClearValue& InClearValue, bool bClearDepth = true, bool bClearStencil = true)
        {
            ClearValue = InClearValue;
            eLoadActions = bClearDepth ? LOAD_ACTION_CLEAR : LOAD_ACTION_LOAD;
            eStencilLoadActions = bClearStencil ? LOAD_ACTION_CLEAR : LOAD_ACTION_LOAD;
        }

        inline void ClearDSV(float fClearDepth, uint32_t uClearStencil, bool bClearDepth = true, bool bClearStencil = true)
        {
            ClearValue.depth = fClearDepth;
            ClearValue.stencil = uClearStencil;
            eLoadActions = bClearDepth ? LOAD_ACTION_CLEAR : LOAD_ACTION_LOAD;
            eStencilLoadActions = bClearStencil ? LOAD_ACTION_CLEAR : LOAD_ACTION_LOAD;
        }

        bool operator==(const KGfxFrameBufferTargetDesc& o) const
        {
            if (pTargetView != o.pTargetView)
            {
                return false;
            }
            if (eLoadActions != o.eLoadActions)
            {
                return false;
            }
            if (eStencilLoadActions != o.eStencilLoadActions)
            {
                return false;
            }
            if (ClearValue != o.ClearValue)
            {
                return false;
            }
            return true;
        }

        bool operator!=(const KGfxFrameBufferTargetDesc& o) const
        {
            return !(*this == o);
        }
    };

    struct KGfxFrameBufferDesc
    {
        std::vector<KGfxFrameBufferTargetDesc> vecFramebufferRTVDesc = {};
        KGfxFrameBufferTargetDesc DSVDesc = {};
        bool bDSVReadOnly = false;

        // 指定当前FrameBuffer的渲染目标尺寸。
        // 若渲染目标存在且Width和Height均为0时，自动获取渲染目标的尺寸。
        // 若无任何渲染目标时，必须指定非0尺寸。
        uint32_t uWidth = 0;
        uint32_t uHeight = 0;

        bool operator==(const KGfxFrameBufferDesc& o) const
        {
            if (DSVDesc.pTargetView != o.DSVDesc.pTargetView)
                return false;

            if (DSVDesc.pTargetView && DSVDesc != o.DSVDesc)
                return false;

            if (bDSVReadOnly != o.bDSVReadOnly)
                return false;

            if (uWidth != o.uWidth)
                return false;

            if (uHeight != o.uHeight)
                return false;

            if (vecFramebufferRTVDesc.size() != o.vecFramebufferRTVDesc.size())
            {
                return false;
            }

            for (int i = 0; i < vecFramebufferRTVDesc.size(); i++)
            {
                if (vecFramebufferRTVDesc[i] != o.vecFramebufferRTVDesc[i])
                {
                    return false;
                }
            }

            return true;
        }

        KGfxFrameBufferTargetDesc& AddRenderTarget(IKGFX_TextureView* InRTV)
        {
            KGfxFrameBufferTargetDesc rtDesc = {};
            rtDesc.SetTarget(InRTV);
            return vecFramebufferRTVDesc.emplace_back(rtDesc);
        }
    };
    class IKGFX_Sampler;
    class IKGFX_SamplerBindlessView : public KGfxRef
    {
    public:
        virtual uint32_t GetBindlessHandle() = 0;
        virtual const KSamplerState& GetSamplerState() = 0;
        virtual IKGFX_Sampler* GetResource() = 0;
    protected:
        virtual ~IKGFX_SamplerBindlessView() = default;
    };
    class IKGFX_Sampler
    {
    public:
        virtual const KSamplerState& GetSamplerState() = 0;
        virtual uintptr_t            GetNativeHandle() = 0;
        virtual IKGFX_SamplerBindlessView* GetBindlessView() = 0;
    protected:
        virtual ~IKGFX_Sampler() = default;
    };

    class IKGFX_ConstBuffer : public KGfxRef
    {
    public:
        virtual bool              Init(uint32_t bufSize, const char* pcszBufferName = nullptr) = 0;
        virtual void              Update(IKGFX_RenderContext* commandBuffer) = 0;
        virtual uint8_t* GetCpuData() = 0;
        virtual uint32_t          GetCBufSize() const = 0;
        virtual IKGFX_BufferView* GetCBV() const = 0;
        virtual IKGFX_Buffer* GetGfxBuffer() const = 0;

        void SetCpuData(const void* pData, uint32_t uSize)
        {
            if (GetCBufSize() >= uSize)
            {
                memcpy(GetCpuData(), pData, uSize);
            }
            else
            {
                ASSERT(0);
            }
        }
        void SetUpdatFrame(int32_t uUpdateFrame)
        {
            m_nUpdateFrame = uUpdateFrame;
        }

        int32_t GetUpdateFrame() const
        {
            return m_nUpdateFrame;
        }

        virtual bool IsStatic() const
        {
            return true;
        }

    private:
        int32_t m_nUpdateFrame = 0;
    };

    class IKGFX_DynamicConstBuffer : public IKGFX_ConstBuffer
    {
    public:
        virtual bool IsStatic() const override
        {
            return false;
        }

        virtual void* MapRange() = 0;
    };

#pragma endregion

    struct KGfxSubResourceData
    {
        const void* pMemData = nullptr; // 指向初始化数据的指针
        uint32_t    uMemByteRowPitch = 0;       // 纹理一行到下一行的字节距离（以字节为单位），若为Block格式，按Block行进行计算。当前参数可以指定为0，表示数据紧凑排列
        uint32_t    uMemByteDepthPitch = 0;       // 3D纹理一个深度层级到下一个层级的字节距离。当前参数可以指定为0，表示数据紧凑排列
    };

    enum BufferUsageFlagBits : uint32_t
    {
        BUFFER_USAGE_TRANSFER_SRC_BIT = 0x00000001,
        BUFFER_USAGE_TRANSFER_DST_BIT = 0x00000002,
        BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT = 0x00000004, // similar to shader resource view via Buffer<format> in hlsl
        BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT = 0x00000008, // similar to unorder resource view via RWBuffer<format> in hlsl
        BUFFER_USAGE_UNIFORM_BUFFER_BIT = 0x00000010,
        BUFFER_USAGE_STORAGE_BUFFER_BIT = 0x00000020,
        BUFFER_USAGE_INDEX_BUFFER_BIT = 0x00000040,
        BUFFER_USAGE_VERTEX_BUFFER_BIT = 0x00000080,
        BUFFER_USAGE_INDIRECT_BUFFER_BIT = 0x00000100,
        BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR = 0x00080000,
        BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR = 0x00100000, // for ray tracing acceleration buffer creation ! enable in vk 1.3
        BUFFER_USAGE_SHADER_BINDING_TABLE_BIT_KHR = 0x00000400, // for ray tracing shader binding table buffer creation! enable in vk 1.3
        BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT = 0x00020000,
        BUFFER_USAGE_FLAG_BITS_MAX_ENUM = 0x7FFFFFFF
    };

    RHI_ENUM_CLASS_OPERATORS(BufferUsageFlagBits);

    using BufferUsageFlags = uint32_t;

    struct KGfxBufferDesc
    {
        uint32_t               uByteWidth;
        BufferUsageFlags       uUsageFlags;
        KGfxResourceAccessType eResAccessFlags;
        uint32_t               uStructureByteStride;
        bool                   bForceStatic = false;
        KGfxResourceMiscFlags  eMiscFlags = 1; ///< 额外的标志位，暂时只支持 AutoLayoutTransition

        bool IsAutoRes() const
        {
            return is_set(static_cast<KGfxResourceMiscFlag>(eMiscFlags), KGfxResourceMiscFlag::AutoLayoutTransition);
        }

        static KGfxBufferDesc GetBufDesc_UBO(uint32_t uByteWidth)
        {
            KGfxBufferDesc bufDesc;
            CHECK_ASSERT(uByteWidth > 0);
            bufDesc.uByteWidth = uByteWidth;
            bufDesc.uUsageFlags = BufferUsageFlagBits::BUFFER_USAGE_UNIFORM_BUFFER_BIT;
            bufDesc.eResAccessFlags = KGfxResourceAccessType::KGfxResourceAccess_GPUOnly;
            bufDesc.uStructureByteStride = 0;
            bufDesc.bForceStatic = true;
            return bufDesc;
        }

        static KGfxBufferDesc GetBufDesc_UBO_DynamicWrite(uint32_t uByteWidth)
        {
            KGfxBufferDesc bufDesc;
            CHECK_ASSERT(uByteWidth > 0);
            bufDesc.uByteWidth = uByteWidth;
            bufDesc.uUsageFlags = BufferUsageFlagBits::BUFFER_USAGE_UNIFORM_BUFFER_BIT;
            bufDesc.eResAccessFlags = KGfxResourceAccessType::KGfxResourceAccess_Write;
            bufDesc.uStructureByteStride = 0;
            bufDesc.bForceStatic = true;
            return bufDesc;
        }

        static KGfxBufferDesc GetBufDesc_GpuRW(uint32_t uByteWidth, BufferUsageFlags uUsageFlags = static_cast<BufferUsageFlagBits>(0))
        {
            KGfxBufferDesc bufDesc;
            CHECK_ASSERT(uByteWidth > 0);
            bufDesc.uByteWidth = uByteWidth;
            bufDesc.uUsageFlags = BufferUsageFlagBits::BUFFER_USAGE_STORAGE_BUFFER_BIT | uUsageFlags;
            bufDesc.eResAccessFlags = KGfxResourceAccessType::KGfxResourceAccess_GPUOnly;
            bufDesc.uStructureByteStride = 0;
            bufDesc.bForceStatic = true;
            return bufDesc;
        }

        static KGfxBufferDesc GetBufDesc_DynamicGpuR(uint32_t uByteWidth, uint32_t InStructStride, BufferUsageFlags uUsageFlags = static_cast<BufferUsageFlagBits>(0))
        {
            KGfxBufferDesc bufDesc;
            CHECK_ASSERT(uByteWidth > 0);
            bufDesc.uByteWidth = uByteWidth;
            bufDesc.uUsageFlags = BufferUsageFlagBits::BUFFER_USAGE_STORAGE_BUFFER_BIT | uUsageFlags;
            bufDesc.eResAccessFlags = KGfxResourceAccessType::KGfxResourceAccess_Write;
            bufDesc.uStructureByteStride = InStructStride;
            bufDesc.bForceStatic = false;
            return bufDesc;
        }

        static KGfxBufferDesc GetBufDesc_CpuWGpuR(uint32_t uByteWidth, BufferUsageFlags uUsageFlags = static_cast<BufferUsageFlagBits>(0))
        {
            KGfxBufferDesc bufDesc;
            CHECK_ASSERT(uByteWidth > 0);
            bufDesc.uByteWidth = uByteWidth;
            bufDesc.uUsageFlags = BufferUsageFlagBits::BUFFER_USAGE_STORAGE_BUFFER_BIT | uUsageFlags;
            bufDesc.eResAccessFlags = KGfxResourceAccessType::KGfxResourceAccess_Write;
            bufDesc.uStructureByteStride = 0;
            bufDesc.bForceStatic = true;
            return bufDesc;
        }
        static KGfxBufferDesc GetBufDesc_GpuFmtRW(uint32_t uByteWidth, BufferUsageFlags uUsageFlags = static_cast<BufferUsageFlagBits>(0))
        {
            KGfxBufferDesc bufDesc;
            CHECK_ASSERT(uByteWidth > 0);
            bufDesc.uByteWidth = uByteWidth;
            bufDesc.uUsageFlags = BufferUsageFlagBits::BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT | uUsageFlags;
            bufDesc.eResAccessFlags = KGfxResourceAccessType::KGfxResourceAccess_GPUOnly;
            bufDesc.uStructureByteStride = 0;
            bufDesc.bForceStatic = true;
            return bufDesc;
        }

        static KGfxBufferDesc GetBufDesc_CPUReadStaging(uint32_t uByteWidth, BufferUsageFlags uUsageFlags = static_cast<BufferUsageFlagBits>(0))
        {
            KGfxBufferDesc bufDesc;
            CHECK_ASSERT(uByteWidth > 0);
            bufDesc.uByteWidth = uByteWidth;
            bufDesc.uUsageFlags = BufferUsageFlagBits::BUFFER_USAGE_TRANSFER_DST_BIT | uUsageFlags;
            bufDesc.eResAccessFlags = KGfxResourceAccessType::KGfxResourceAccess_Read;
            bufDesc.uStructureByteStride = 0;
            bufDesc.bForceStatic = true;
            return bufDesc;
        }
    };

    class KSignalFence : public KGfxRef
    {
    public:
        struct Desc
        {
            uint64_t initialValue = 0;
            bool     isShared = false;
        };

        KSignalFence() = default;
        virtual ~KSignalFence() override = default;

        virtual bool IsSubmitted() const = 0;
        virtual void Clear() = 0;
        virtual bool Query() = 0;

        /// Returns the currently signaled value on the device.
        virtual bool GetCurrentValue(uint64_t* outValue) = 0;

        virtual void* GetNativeHandle() = 0;
    };

    class IKGFX_Resource : public KGfxRef
    {
    public:
        // 返回底层图形API的资源接口
        virtual uintptr_t GetNativeResourceHandle() = 0;

        virtual void SetDebugName(const char* name) = 0;
        virtual const char* GetDebugName() = 0;
    };

    class IKGFX_TextureResource : public IKGFX_Resource
    {
    public:
        virtual const KGFX_TextureDesc* GetDesc() const = 0;
        virtual uint32_t GetDeviceMemorySize() const = 0;
    };

    class IKGFX_Buffer : public IKGFX_Resource
    {
    public:
        virtual const KGfxBufferDesc* GetDesc() const = 0;
        //bOverWrite false 表示分配新的， true 表示不分配新的，覆盖原来的数据, 默认为false
        virtual BOOL      Update(const void* pSrcData, uint32_t uSrcDataSize, uint32_t uDstOffset, BOOL bOverWrite) = 0;
        virtual void* MapRange() = 0;
        virtual BOOL      IsDynamic() = 0;
        virtual uint32_t  GetDynamicOffset() = 0;
        virtual uint64_t  GetBufferDeviceAddress() = 0;
    };

    class KRenderTarget : public KGfxRef
    {
    protected:
        KRenderTargetDesc    mDesc;
        uint32_t m_uTextureUpdateCode = 0;
        uint32_t m_uTextureId = 0;

    public:
        virtual ~KRenderTarget() {};

        virtual IKGFX_TextureView* GetFullRTV() const { return nullptr; }                                           // TODO: = 0 later
        virtual IKGFX_TextureView* GetFullDSV() const { return nullptr; }                                           // TODO: = 0 later
        virtual IKGFX_TextureView* GetFullSRV() const { return nullptr; }                                           // TODO: = 0 later
        virtual IKGFX_TextureView* GetFullUAV() const { return nullptr; }                                           // TODO: = 0 later
        virtual IKGFX_TextureView* GetMipSRV(uint32_t MipLevel, uint32_t uArraySlice = 0) const { return nullptr; } // TODO: = 0 later
        virtual IKGFX_TextureView* GetMipUAV(uint32_t MipLevel, uint32_t uArraySlice = 0) const { return nullptr; } // TODO: = 0 later
        virtual IKGFX_TextureView* GetMipRTV(uint32_t MipLevel, uint32_t uArraySlice = 0) const { return nullptr; } // TODO: = 0 later
        virtual IKGFX_TextureView* GetMipDSV(uint32_t MipLevel, uint32_t uArraySlice = 0) const { return nullptr; } // TODO: = 0 later

        virtual uint32_t GetWidth() const { return mDesc.uWidth; };
        virtual uint32_t GetHeight() const { return mDesc.uHeight; };
        virtual uint32_t GetArraySize() const { return mDesc.uArraySize; }
        virtual const KRenderTargetDesc& GetDesc() { return mDesc; };

        virtual bool IsForDepth() = 0;
        virtual bool IsHasStencil() = 0;
        virtual void SetObjectName(const char* szName) = 0;
        virtual bool SaveToFile(const char* pcszSaveFilePath) { return FALSE; }

    public:
        virtual const char* GetName() { return ""; }
        virtual uint64_t    GetNameHash() = 0;

        virtual IKGFX_TextureView* GetSRV() const = 0;
        virtual IKGFX_TextureView* GetUAV() const = 0;
        virtual IKGFX_TextureResource* GetTextureResource() const = 0;

        /// 返回图形API最底层的数据  比如ID3D12Resource vkimage // TODO: 后续可以删除这个接口，从IKGFX_TextureResource中获取
        virtual void* GetNativeImageHandle() const = 0;

        virtual const KGFX_TextureDesc& GetTexDesc() const = 0;
        virtual KGfxSubresourceRange ResolveSubresourceRange(const KGfxSubresourceRange& range) = 0;
        virtual uint32_t GetMipMapCount() = 0;

        virtual BOOL IsRenderTarget() const = 0;
        virtual uint64_t GetId() = 0;
        virtual uint64_t GetResourceSize() = 0;

    protected:
        gfx::IKGFX_TextureView* m_pBindlessSRV = nullptr; // 绑定的Bindless SRV视图
        gfx::IKGFX_TextureView* m_pBindlessUAV = nullptr; // 绑定的Bindless UAV视图
    };

    class IKGFX_RenderFrameBuffer;
    class IKGFX_RenderContext : public KGfxRef
    {
    public:
        virtual ~IKGFX_RenderContext() override = default;

        virtual bool IsValid() const = 0;

        virtual void* GetCommandBufferNativeHandle() const = 0;
        virtual BOOL BeginCommandBuffer() = 0;
        virtual void SubmitCommandBuffer(BOOL bWait = FALSE, void* pGpuCompletedSignal = nullptr) = 0;

    public:
        // 先不考虑同时清楚多个子区域的情况，需要的话重载接口实现吧
        virtual void CmdClearAttachment(const KClearAttchment* pAttachment, int nCount, const NSKMath::KVectorInt2& v2Offset, const NSKMath::KVectorUint2& v2Size, uint32_t uBaseArrayLayer = 0, uint32_t uLayerCount = 1) = 0;
        virtual void CmdClearTextureView(IKGFX_TextureView* view, KClearValue clearValue = KClearValue::GetZero(), KGFX_ClearResourceViewFlags flags = KGFX_ClearResourceViewFlags::FloatClearValues) = 0;
        virtual void CmdClearBufferView(IKGFX_BufferView* view, KClearValue clearValue = KClearValue::GetZero(), KGFX_ClearResourceViewFlags flags = KGFX_ClearResourceViewFlags::FloatClearValues) = 0;

    public:
        virtual void CmdSetLineWidth(float linewidth) = 0;
        virtual void CmdSetDepthBias(float fConstant, float fClamp, float fSlop, bool bAutoReverse = true) = 0;

        virtual void CmdSetScissor(int nX, int nY, int nWidth, int nHeight) = 0;
        virtual void CmdSetScissor(const IKGFX_RenderFrameBuffer* piRenderFrameBuffer) = 0;

        virtual void CmdSetViewport(const ViewportDescription& Viewport) = 0;
        virtual void CmdSetViewport(const IKGFX_RenderFrameBuffer* piRenderFrameBuffer) = 0;

        virtual void CmdBindVertexBuffers(int nFirstBinding, int nBindingCount, IKGFX_Buffer* apBuffer[], int anOffsets[], uint32_t* stride = nullptr) = 0;
        virtual void CmdBindIndexBuffer(IKGFX_Buffer* pBuffer, int nOffset, enumIndexType indexType) = 0;

        virtual void CmdDraw(int nVertexCount, int nFirstVertex, bool bPoint = false) = 0;
        virtual void CmdDrawInstanced(int nVertexCount, int nFirstVertex, int nInstanceCount, int nFirstInstance) = 0;
        virtual void CmdDrawIndexed(int nIndexCount, int nInstanceCount, int nFirstIndex, int nVertexOffset, int nFirstInstance) = 0;
        virtual void CmdDrawIndexedInstanced(int nIndexCount, int nFirstIndex, int nInstanceCount, int nFirstVertex, int nFirstInstance) = 0;
        virtual void CmdDrawIndexedIndirect(IKGFX_Buffer* pInderiectCommandBuffer, int nOffset, int nDrawCount, int nStride, bool bRecordDrawCall = true) = 0;
        virtual void CmdDrawIndirect(IKGFX_Buffer* pInderiectCommandBuffer, int nOffset, int nDrawCount, int nStride) = 0;
        virtual void CmdBeginUAVOverlap(IKGFX_Resource* const* ppResourceUAV, uint32_t count) = 0;
        virtual void CmdEndUAVOverlap(IKGFX_Resource* const* ppResourceUAV, uint32_t count) = 0;

    public:
        virtual BOOL CmdDispatch(int nGroupCountX, int nGroupCountY, int nGroupCountZ) = 0;
        virtual BOOL CmdDispatchIndirect(IKGFX_Buffer* pIndirectBuffer, int nOffset) = 0;

    public:
        virtual BOOL Transition(const KGfxBarrier& sBarrierInfo) = 0;
        virtual BOOL Transition(const std::initializer_list<KGfxBarrier>& lsBarrierInfosArray) = 0;
        virtual BOOL Transition(const KGfxBarrier* pBarrierInfos, uint32_t uBarrierCount) = 0;

    public:
        virtual void CmdUpdateSubResource(IKGFX_Buffer* pGfxBuffer, uint32_t uOffset, uint32_t uSize, const void* pData, uint32_t option = 0) = 0;
        virtual void CmdUpdateAllResource(IKGFX_TextureResource* pGfxTexure, std::vector<gfx::KGfxSubResourceData>& data) = 0;
        virtual void CmdUpdateSubResource(IKGFX_TextureResource* pGfxTexure, uint32_t uDstMipLevel, uint32_t uDstArraySlice, const KGfxCopyRegion* pDstRegion, const void* pSrcData, uint32_t uSrcRowPitch, uint32_t SrcDepthPitch) = 0;

        virtual void CmdCopyBuffer(IKGFX_Buffer* pSrcBuffer, IKGFX_Buffer* pDstBuffer) = 0;
        virtual void CmdCopyBufferSubRegions(IKGFX_Buffer* pSrcBuffer, IKGFX_Buffer* pDstBuffer, uint32_t uCopyRegionCount, const KBufferCopyRegion* pCopyRegions) = 0;
        virtual void CmdCopyTexture(IKGFX_TextureResource* pSrcTexture, IKGFX_TextureResource* pDstTexture) = 0;
        virtual void CmdCopyTextureSubRegions(IKGFX_TextureResource* pSrcTexture, IKGFX_TextureResource* pDstTexture, uint32_t uCopyRegionCount, const KTextureCopyRegion* pCopyRegions) = 0;

        // 拷贝纹理到缓冲区
        // pBufferTextureCopy和NumBufferTextureCopy为nullptr和0时，表示拷贝整个纹理到缓冲区
        virtual void CmdCopyTextureToBuffer(IKGFX_TextureResource* pSrcTexture, IKGFX_Buffer* pDstBuffer, const KBufferTextureCopy* pBufferTextureCopy, uint32_t NumBufferTextureCopy) = 0;

        // 拷贝缓冲区到纹理
        // pBufferTextureCopy和NumBufferTextureCopy为nullptr和0时，表示拷贝整个缓冲区到纹理
        virtual void CmdCopyBufferToTexture(IKGFX_Buffer* pSrcBuffer, IKGFX_TextureResource* pDstTexture, const KBufferTextureCopy* pBufferTextureCopy, uint32_t NumBufferTextureCopy) = 0;

    public:
        virtual BOOL CmdInsertSignalFence(KSignalFence* pSignalFence) = 0;
        virtual void CmdClose() = 0;

    public:
        virtual void BeginDebugLabel(const char* strDebugLabel) = 0;
        virtual void EndDebugLabel() = 0;

        virtual void BeginOptickProfile() = 0;
        virtual void EndOptickProfile() = 0;
    };

    struct KSpecializationConstDefine
    {
        uint32_t       uStageType;
        uint32_t       uConstId;
        const_pool_str pName;
    };

    enum enumSpecializationConstantType
    {
        FLOAT_CONSTANT_TYPE,
        INT_CONSTANT_TYPE,
        UINT_CONSTANT_TYPE,
    };

    struct KSpecializationConstant
    {
        uint32_t                       shaderStage_id = 0;
        uint32_t                       constant_id = 0;
        enumSpecializationConstantType constant_type = FLOAT_CONSTANT_TYPE;
        uint32_t                       size = 0;
        union {
            int32_t  nValue = 0;
            uint32_t uValue;
            float    fValue;
        };
    };

    struct KSpecializationConstItem
    {
        uint32_t                       stage_id;
        uint32_t                       uConstId;
        enumSpecializationConstantType const_type;
        const_pool_str                 pName;
        union {
            uint32_t uValue;
            int32_t  nValue;
            float    fValue;
        };
    };
    class KRayTracingScene;
    class IKGFX_ProgramBinder
    {
    public:
        IKGFX_ProgramBinder() = default;
        virtual ~IKGFX_ProgramBinder() = default;

        virtual IKGFX_ProgramBinder& BeginBind(const gfx::RenderCommonParam* pRenderCommanParam, gfx::IKSharedPreBinder* pShareBinder) = 0;

        /// 废弃的接口
        //virtual IKGFX_ProgramBinder& AutoBindSSBO(const_pool_str pcszName, IKGFX_Buffer* pSSBO, const char* pcszBlockName = nullptr) = 0;

        // 
        virtual IKGFX_ProgramBinder& AddBindUAV(const_pool_str pcszName, IKGFX_BufferView* pUAV) = 0;
        virtual IKGFX_ProgramBinder& AddBindUAV(const_pool_str pcszName, IKGFX_TextureView* pTexView) = 0;

        virtual IKGFX_ProgramBinder& AddBindSRV(const_pool_str pcszName, IKGFX_BufferView* pSRV) = 0;
        virtual IKGFX_ProgramBinder& AddBindSRV(const_pool_str pcszName, IKGFX_TextureView* pTexView) = 0;

        // 当前接口用于绑定单寄存器的对象数组
        virtual IKGFX_ProgramBinder& AddBindUAVArray(const_pool_str pcszName, uint32_t uNum, IKGFX_BufferView** pBufViews) = 0;
        virtual IKGFX_ProgramBinder& AddBindUAVArray(const_pool_str pcszName, uint32_t uNum, IKGFX_TextureView** pTexViews) = 0;

        // 当前接口用于绑定单寄存器的对象数组
        virtual IKGFX_ProgramBinder& AddBindSRVArray(const_pool_str pcszName, uint32_t uNum, IKGFX_BufferView** pBufViews) = 0;
        virtual IKGFX_ProgramBinder& AddBindSRVArray(const_pool_str pcszName, uint32_t uNum, IKGFX_TextureView** pTexViews) = 0;

        virtual IKGFX_ProgramBinder& AddBindCBV(const_pool_str pcszName, IKGFX_BufferView* pBufView) = 0;
        virtual IKGFX_ProgramBinder& AddBindSampler(const_pool_str pcszName, IKGFX_Sampler* pSampler) = 0;
        //绑定光追场景加速结构
        virtual IKGFX_ProgramBinder& AddBindAccelerationStructure(const_pool_str pcszName, KRayTracingScene* accelerationStructure) = 0;

        //vulkan对应的是 specialzationConst的实现能有效在pipeline上动态编译分支，达到宏分支的效果，对vk而言是很不错的优化能有效减少宏变体，dx用的cbuffer
        virtual IKGFX_ProgramBinder& SetImmutableConstValueInt(const_pool_str pcszName, int32_t value) = 0;
        virtual IKGFX_ProgramBinder& SetImmutableConstValueUInt(const_pool_str pcszName, uint32_t value) = 0;
        virtual IKGFX_ProgramBinder& SetImmutableConstValueFloat(const_pool_str pcszName, float value) = 0;


        virtual BOOL IsTextureBinded(const_pool_str pName) = 0;
        virtual BOOL UpdateMtlData() = 0;

        virtual BOOL        SetMtlParamValue(const_pool_str szName, void* pData, uint32_t uByteSize) = 0;
        virtual BOOL        IsBinding() = 0;
        virtual TextureType GetTextureType(const_pool_str szName) = 0;

    private:
        virtual BOOL EndBind() = 0;
    };

    class IKGFX_Program : public KGfxRef
    {
    public:
        virtual int32_t              Release() override;
        uint32_t                     m_delayReleaseCounter = DELAY_RELEASE_FRAME_COUNT;
        virtual IKGFX_ProgramBinder* GetProgramBinder() = 0;
        virtual BOOL LoadFromFile() = 0;
        virtual BOOL IsLoading() = 0;
        virtual BOOL IsLoaded() = 0;
        virtual BOOL IsLoadFailed() = 0;
        virtual uint32_t GetCurrentPipelineCode() = 0;
        struct KGFX_ProgramLoadParam
        {
            std::string              m_szShaderSource;
            NSKBase::tagFileLocation m_sUserShaderLoc;
            std::string              m_szShaderDef;
            std::string              m_szMacro;
            BOOL                     m_bReCreate;
            BOOL                     m_bByBuildToolCmd;
            int                      m_nPlatform;            
            KEnumMtlTaskLevel        m_uThreadLevel;

            int32_t m_nMaterialID = 0;
            int32_t m_nReflectionID = 0;
            char m_cVaryingMask = 0;
        };
    public:
        BOOL m_bByPreForceLoad = false; //如果开启这种模式加载，后面pipeline的创建强行改成单线程模式，不然要用的那帧还是画不出东西，也就失去预加载的意义了
    };

    class IKGFX_GraphicsProgram : public IKGFX_Program
    {
    public:
        IKGFX_GraphicsProgram()
        {
        }
        virtual ~IKGFX_GraphicsProgram()
        {
        }
        virtual BOOL LoadGraphicsShader(
            const char* szShaderSource, const NSKBase::tagFileLocation& sIncludeShaderLoc,
            const char* szShaderDef, const char* szMacro,
            BOOL bReCreate, BOOL bByBuildToolCmd = false, int nPlatform = 0,
            KEnumMtlTaskLevel uThreadLevel = KEnumMtlTaskLevel::DISABLE_MTL_THREAD
        ) = 0;
        
        virtual BOOL PostLoad() = 0;        

        virtual void                   ClearVertDesc() = 0;
        virtual IKGFX_GraphicsProgram& BeginVertDesc() = 0;
        virtual IKGFX_GraphicsProgram& AddBindDescription(uint32_t binding, uint32_t stride, enumVertexInputRate inputRate) = 0;
        virtual IKGFX_GraphicsProgram& AddAttribute(gfx::KVertexDecl* pDecl, uint32_t binding, uint32_t location, enumVertexFormat format, uint32_t offset) = 0;
        virtual BOOL                   EndVertDesc() = 0;

        virtual BOOL BindVertAttr(gfx::KVertexDecl* pDecls[], uint32_t uDeclCount) = 0;
        virtual BOOL ApplyRenderState(const std::function<void(gfx::KRenderState*)>& fnRenderStateDefineCall, gfx::KRenderState* pRenderState = nullptr) = 0;
        virtual BOOL IsReady() = 0;

        // 后面统一成这个接口，pushconst将不分shader类别
        virtual BOOL SetConstDataBlock(uint32_t uSize, void* pData) = 0;

        //      virtual IKGFX_ProgramBinder& BeginBind() = 0;
        // virtual BOOL EndBind(uint64_t uDescriptorSetId) = 0;
        virtual BOOL SetMtlParamValue(const_pool_str szName, void* pData, uint32_t uByteSize) = 0;
        virtual BOOL PreparePipeline() = 0;
        virtual BOOL ApplyPipeline() = 0;
        virtual BOOL UpdateMtlData() = 0;

        virtual IKGFX_ProgramBinder& BeginBind(const gfx::RenderCommonParam* pRenderCommanParam, gfx::IKSharedPreBinder* pShareBinder) = 0;

        virtual BOOL                     EndBind() = 0;
        virtual void                     SwapBindData(IKGFX_GraphicsProgram* pProgram) = 0;
        virtual BOOL                     IsTextureBinded(const_pool_str pName) = 0;
        virtual const gfx::KRenderState& GetSrcRenderState() = 0;
        virtual gfx::KRenderState* GetRenderState() = 0;
        virtual BOOL                     IsBeginBind() = 0;
        virtual void                     SetBeginBind(BOOL bBeginBind) = 0;
        virtual BOOL                     IsActiveBlock(const_pool_str pcszName) = 0;
        
        virtual void GetUserShaderDetail(int32_t& nMaterialID, int32_t& nReflectionID, char& cVaryingMask) = 0;


        virtual void AddSamplerState(const char* pName, gfx::KSamplerState& samplerState) = 0;
    };

    class IKGFX_ComputeProgram : public IKGFX_Program
    {
    public:
        IKGFX_ComputeProgram() {}
        virtual ~IKGFX_ComputeProgram() {}

        virtual BOOL LoadComputeShader(const char* pcszShaderSource, const NSKBase::tagFileLocation& sIncludeShaderLoc, const char* pcszShaderDef, const char* pcszMacro, BOOL bByBuildToolCmd = false, int nPlatform = 0, KEnumMtlTaskLevel uThreadLevel = KEnumMtlTaskLevel::DISABLE_MTL_THREAD) = 0;

        virtual BOOL Apply(IKGFX_RenderContext* pRenderCtx) = 0;

        virtual BOOL                 SetConstDataBlock(gfx::IKGFX_RenderContext* pRenderCtx, uint32_t uSize, void* pData) = 0;
        virtual IKGFX_ProgramBinder& BeginBind() = 0;
        virtual IKGFX_ProgramBinder& BeginBind(const gfx::RenderCommonParam* pRenderCommanParam, gfx::IKSharedPreBinder* pShareBinder) = 0;
        virtual BOOL                 EndBind() = 0;
        virtual BOOL                 IsReady() = 0;
        virtual void                 SetBeginBind(BOOL bBeginBind) = 0;
    };

    class IKGFX_Swapchain
    {
    public:
        virtual ~IKGFX_Swapchain()
        {
        }
        virtual BOOL                Init(const KWindow* pWindowInfo) = 0;
        virtual BOOL                UnInit() = 0;
        virtual BOOL                BeginRender() = 0;
        virtual BOOL                Present() = 0;
        virtual BOOL                OnResize() = 0;
        virtual KWindow* GetWindow() = 0;
        virtual uint32_t            GetSwapChainRTCount() = 0;
        virtual gfx::KRenderTarget* GetCurerntSwapChainRT() = 0;
        virtual gfx::KRenderTarget* GetDepthStencilRT() = 0;
        virtual BOOL                IsBeginRender() = 0;
        virtual BOOL                IsAcquiredImage() = 0;

        // virtual void CmdExecuteCmdBuf(IKGFX_RenderContext* cmdBuf, KSignalFence* fenceToSingle = nullptr, uint64_t newFenceValue = 0) = 0;
        // virtual void CmdExecuteCmdBufs(IKGFX_RenderContext** cmdBuf, int cmdbufCount, KSignalFence* fenceToSingle = nullptr, uint64_t newFenceValue = 0) = 0;

        virtual enumTextureFormat GetSwapChainColorFormat() = 0;
        virtual enumTextureFormat GetSwapChainDepthFormat() = 0;

    public:
        uint32_t m_nRenderBufferWidth = 0;
        uint32_t m_nRenderBufferHeight = 0;
        void* m_pWindowHandle = nullptr;
        BOOL     m_bNeedOnResize = true;    // 默认 绘制开始 触发一次
        BOOL     m_bNeedSwapChainResize = false;
    };

    class IKGFX_RenderFrameBuffer : public KGfxRef
    {
    public:
        virtual ~IKGFX_RenderFrameBuffer() {}

        virtual uint32_t GetWidth() const = 0;
        virtual uint32_t GetHeight() const = 0;
        virtual uint32_t GetRenderTargetCount() const = 0;

        virtual BOOL BeginPass(IKGFX_RenderContext* pRenderCtx, bool bUseBarrier = true, bool bImmediateMode = false) = 0;
        virtual BOOL EndPass(IKGFX_RenderContext* pRenderCtx) = 0;

        virtual void SetObjectName(const char* szName) = 0;
    };

    struct TextureFormatInfo
    {
        uint32_t uBytesPerBlock;
        uint32_t uWidthPerBlock;
        uint32_t uHeightPerBlock;
    };
    struct IKRayTracingProxy;
    class IKGFX_GraphicDevice
    {
    public:
        virtual ~IKGFX_GraphicDevice() {}
        virtual BOOL Init(const gfx::RenderSystemInfo& renderSysteInfo) = 0;
        virtual void UnInit() = 0;

        virtual void Setup(const KWindow* pWindowInfo) = 0;
        virtual void FrameMove(BOOL bFrameRendered) = 0;
        virtual void DeviceWaitIdle() = 0;

        virtual void ReleaseAllDeviceObjects() = 0;
        virtual void ReleaseAllProgram() = 0;

        // property
        virtual BOOL IsInitedGraphic() = 0;
        virtual const char* GetDeviceName() const = 0;
        virtual TextureFormatInfo GetTextureFormatInfo(enumTextureFormat eFormat) const = 0;

        // support extension
        virtual BOOL IsFp16Supported() const = 0;
        virtual BOOL IsSubGoupQuadSupported() const = 0;
        virtual BOOL IsSubGoupF16Supported() const = 0;
        virtual BOOL IsAtomicUint64Supported() const = 0;

        // physical device limits
        virtual const KGFX_PHYSICAL_DEVICE_LIMITS& GetPhysicalDeviceLimits() const = 0;

        // swap chain
        virtual IKGFX_Swapchain* GetContext(enumGraphicContext contextType) = 0;
        virtual void DeleteContext(enumGraphicContext contextType) = 0;

        // render context(command buffer)
        virtual IKGFX_RenderContext* GetRenderContext() = 0;

        // frame buffer
        virtual BOOL CreateRenderFrameBuffer(gfx::IKGFX_RenderFrameBuffer** ppRetRenderFrameBuffer, const KGfxFrameBufferDesc& fbDesc) = 0;

        // program
        virtual IKGFX_GraphicsProgram* CreateGraphicsProgram() = 0;
        virtual IKGFX_ComputeProgram* CreateComputeProgram() = 0;
        virtual BOOL DestroyProgram(IKGFX_Program*& pProgram) = 0;

        // sampler
        virtual IKGFX_Sampler* GetSamplerByState(KSamplerState* pSamplerState) = 0;
        

        // texture
        virtual bool CreateTexture(const KGFX_TextureDesc& texDsec, const char* szDebugName, IKGFX_TextureResource** outResource) = 0;
        virtual bool CreateTextureView(IKGFX_TextureResource* texResource, KGFX_TextureViewDesc const& viewDesc, IKGFX_TextureView** outView) = 0;

        // buffer
        virtual BOOL CreateBuffer(IKGFX_Buffer** ppRetBuffer, const KGfxBufferDesc& bufDesc, const void* pData) = 0;
        virtual BOOL CreateBufferView(IKGFX_Buffer* pBuffer, const KGFX_BufferViewDesc& sViewDesc, IKGFX_BufferView** pRefBufferView, const char* pcszDebugName) = 0;

        virtual IKGFX_Buffer* CreateDynamicBuffer(const KGfxBufferDesc& bufDesc, BOOL bShareMode = true) = 0;
        virtual int GetDynamicBufferCount() = 0;

        virtual bool CreateDynamicConstBuf(IKGFX_DynamicConstBuffer** ppRetConstBuffer, uint32_t size, const char* pDebugName = nullptr) = 0;
        virtual bool CreateStaticConstBuf(IKGFX_ConstBuffer** ppRetConstBuffer, uint32_t size, const char* pDebugName = nullptr) = 0;

        // render target
        virtual BOOL CreateRenderTarget(KRenderTarget** ppRenderTarget, KRenderTargetDesc* pRenderTargetDesc, BOOL bTileOptimize = false, uint64_t* pRetCheckCode = nullptr) = 0;

        // fence
        virtual BOOL CreateSignalFence(KSignalFence** ppRetSignalFence) = 0;

        virtual void* LoadRayTracShader(
            const char* pMainShader,
            const char* pEnterpoint,
            const NSKBase::tagFileLocation& sUserShaderLoc,
            const char* szMacro,
            gfx::ShaderStageType eShaderStage,
            BOOL bByBuildToolCmd = false,
            int nPlatform = 0) = 0;

#if bXR_ON
        XRLocalViewDataLH* XRGetLocalViewData(float* nearSee, float* farSee) = 0;
        uint32_t           GetXRSwapchainWidth() = 0;
        uint32_t           GetXRSwapchainHeight() = 0;
#endif // #if bXR_ON

        virtual void* GetNativeGraphicQueue() const = 0;
        virtual void* GetNativeGraphicDevice() const = 0;

        virtual void* GetNativePhysicsDevice() const { return nullptr; } // 1、vulkan: VkPhysicalDevice; 2、dx12: ID3D12Device
        virtual void* GetNativeGraphicInstance() const { return nullptr; }  // 1、vulkan: VkInstance; 2、dx12: IDXGIFactory
        virtual IKGFX_PipelineLoadThread* CreatePipelineLoadThread() = 0;
        virtual BOOL InitShaderResourcePool() = 0;
        virtual void UnInitShaderResourcePool() = 0;
        // 用于dump设备内存信息
        virtual void DumpDeviceMemoryInfo(std::function<void(const char*, uint32_t)> const& outputFunc) = 0;
        //raytracing proxy
        virtual IKRayTracingProxy* CreateRayTracingProxy() = 0;
    };

    ///////////////////////////////////////////////////////////////////////////////////////////////
    BOOL                 KGFX_InitRenderSystem(RenderSystemInfo& renderSysteInfo);
    void                 KGFX_UninitRenderSystem();
    BOOL                 KGFX_CreateContext(const NSEngine::KWindowInfo& WindowInfo, enumGraphicContext eGraphicContext);
    IKGFX_GraphicDevice* KGFX_GetGraphicDevice();

    IKGFX_RenderContext* GetRenderContext();

} // namespace gfx

#define KMAX_BLEND_ATTACHMENT 5
namespace gfx
{
    // KRenderstate changed to this class, use c++ operator overload skill
    // every property modify will be automatic update state hash as the render state key
    class KRenderState
    {
    private:
        struct data
        {
            mutable uint32_t hash = 0;
            struct _dt
            {
                _dt();
                float depthBiasConstantFactor = 0.f;
                float depthBiasClamp = 0.f;
                float depthBiasSlopeFactor = 0.f;
                float minDepthBounds = 0.f;
                float maxDepthBounds = 1.f;
                float lineWidth = 1.f;

                struct
                {
                    float x = 0.f;
                    float y = 0.f;
                    float width = 1.f;
                    float height = 1.f;
                    float minDepth = 0.f;
                    float maxDepth = 1.f;
                } viewPort;

                // int32_t depthBiasConstantFactor = 0.f;
                // int32_t depthBiasClamp          = 0.f;
                // int32_t depthBiasSlopeFactor    = 0.f;
                // int32_t minDepthBounds          = 0.f;
                // int32_t maxDepthBounds          = 1.f;
                // int32_t lineWidth               = 1.f;

                // struct
                //{
                //	int32_t x        = 0.f;
                //	int32_t y        = 0.f;
                //	int32_t width    = 1.f;
                //	int32_t height   = 1.f;
                //	int32_t minDepth = 0.f;
                //	int32_t maxDepth = 1.f;
                // } viewPort;

                struct _scissor
                {
                    int32_t  offsetX = 0;
                    int32_t  offsetY = 0;
                    uint32_t extendWidth = 1024;
                    uint32_t extendHeight = 1024;
                } scissor;

                uint8_t             msaa = 0;
                uint8_t             msaa_line = 0;
                uint8_t             sampleShadingEnable = 0;
                enumSampleCountFlag sampleCountFlag = SAMPLE_COUNT_1_BIT;
                uint8_t             sampleAlphaToCoverageEnable = 0;
                uint8_t             sampleAlphaToOneEnable = 0;
                uint32_t            sampleMask = 0xffffffff;
                float               minSampleShading = 1.0f;

                enumPolygonMode   polygonMode = POLYGON_MODE_FILL;
                enumFrontFaceMode frontFaceMode = FRONT_FACE_COUNTER_CLOCKWISE;
                enumCullMode      cullMode = CULL_MODE_BACK;
                enumDrawMode      drawMode = PT_TRIANGLE_LIST;
                uint8_t           depthBiasEnable = 0;
                uint8_t           depthClampEnable = 0;
                uint8_t           rasterizerDiscardEnable = 0;
                uint8_t           blendAttachCount = 1;
                uint8_t           mrtMask = 0;

                uint8_t alphaRef = 128;
                uint8_t pointSize = 16;

                uint8_t       depthTestEnable = 1;
                uint8_t       depthWriteEnable = 1;
                enumDepthType depthCompareOp = DrvOption::bReversePerspectiveDepthZ ? gfx::DEPTH_TEST_GEQUAL : gfx::DEPTH_TEST_LEQUAL;
                uint8_t       depthBoundsTestEnable = 0;

                uint8_t stencilTestEnable = 0;

                struct _stencil
                {
                    uint32_t          compareMask = 0xffffff;
                    uint32_t          writeMask = 0xffffff;
                    short             reference = 0;
                    enumStencilType   stencilCompareOp = STENCIL_TEST_LESS;
                    enumStencilOpType sencilFailOp = STENCIL_OP_KEEP;
                    enumStencilOpType stencilPassOp = STENCIL_OP_KEEP;
                    enumStencilOpType stencilDepthFailOp = STENCIL_OP_KEEP;
                } stencilFront, stencilBack;

                struct _blendAttach
                {
                    uint8_t blendEnable = 0;
                    uint8_t writeR = 1;
                    uint8_t writeG = 1;
                    uint8_t writeB = 1;
                    uint8_t writeA = 1;

                    enumBlendType srcColorBlendFactor = BLEND_ONE;
                    enumBlendType dstColorBlendFactor = BLEND_ZERO;
                    enumBlendType srcAlphaBlendFactor = BLEND_ONE;
                    enumBlendType dstAlphaBlendFactor = BLEND_ZERO;

                    enumBlendEquationType colorBlendOp = BLEND_EQUATION_ADD;
                    enumBlendEquationType alphaBlendOp = BLEND_EQUATION_ADD;

                } blendAttachment[KMAX_BLEND_ATTACHMENT];

                uint8_t defaultViewPortEnable = 1;
                uint8_t defaultScissorEnable = 1;

            } dt;
            void ZeroHash();
        } d;

    public:
        // DeclareProperty(KRenderState, float, depthBiasConstantFactor);
        struct _depthBiasConstantFactor
        {
            _depthBiasConstantFactor& operator=(const float& other);
            operator float();
        } depthBiasConstantFactor;

        // DeclareProperty(KRenderState, float, depthBiasClamp);
        struct _depthBiasClamp
        {
            _depthBiasClamp& operator=(const float& other);
            operator float();
        } depthBiasClamp;


        // DeclareProperty(KRenderState, float, depthBiasSlopeFactor);
        struct _depthBiasSlopeFactor
        {
            _depthBiasSlopeFactor& operator=(const float& other);
            operator float();
        } depthBiasSlopeFactor;


        // DeclareProperty(KRenderState, float, minDepthBounds);
        struct _minDepthBounds
        {
            _minDepthBounds& operator=(const float& other);
            operator float();
        } minDepthBounds;

        // DeclareProperty(KRenderState, float, maxDepthBounds);
        struct _maxDepthBounds
        {
            _maxDepthBounds& operator=(const float& other);
            operator float();
        } maxDepthBounds;

        // DeclareProperty(KRenderState, float, lineWidth);
        struct _lineWidth
        {
            _lineWidth& operator=(const float& other);
            operator float();
        } lineWidth;

        struct _viewPort
        {
            // DeclareSecondProperty(KRenderState, float, viewPort, x);
            struct _x
            {
                _x& operator=(const float& other);
                operator float();
            } x;

            // DeclareSecondProperty(KRenderState, float, viewPort, y);
            struct _y
            {
                _y& operator=(const float& other);
                operator float();
            } y;

            // DeclareSecondProperty(KRenderState, float, viewPort, width);
            struct _width
            {
                _width& operator=(const float& other);
                operator float();
            } width;

            // DeclareSecondProperty(KRenderState, float, viewPort, height);
            struct _height
            {
                _height& operator=(const float& other);
                operator float();
            } height;

            // DeclareSecondProperty(KRenderState, float, viewPort, minDepth);
            struct _minDepth
            {
                _minDepth& operator=(const float& other);
                operator float();
            } minDepth;

            // DeclareSecondProperty(KRenderState, float, viewPort, maxDepth);
            struct _maxDepth
            {
                _maxDepth& operator=(const float& other);
                operator float();
            } maxDepth;

        } viewPort;

        struct _scissor
        {
            // DeclareSecondProperty(KRenderState, int32_t, scissor, offsetX);
            struct _offsetX
            {
                _offsetX& operator=(const int32_t& other);
                operator int32_t();
            } offsetX;

            // DeclareSecondProperty(KRenderState, int32_t, scissor, offsetY);
            struct _offsetY
            {
                _offsetY& operator=(const int32_t& other);
                operator int32_t();
            } offsetY;

            // DeclareSecondProperty(KRenderState, uint32_t, scissor, extendWidth);
            struct _extendWidth
            {
                _extendWidth& operator=(const uint32_t& other);
                operator uint32_t();
            } extendWidth;

            // DeclareSecondProperty(KRenderState, uint32_t, scissor, extendHeight);
            struct _extendHeight
            {
                _extendHeight& operator=(const uint32_t& other);
                operator uint32_t();
            } extendHeight;

        } scissor;

        // DeclareProperty(KRenderState, uint8_t, writeZ);
        // struct _writeZ
        //{
        //	_writeZ& operator=(const uint8_t& other);
        //	operator uint8_t();
        // } writeZ;


        // DeclareProperty(KRenderState, uint8_t, msaa);
        struct _msaa
        {
            _msaa& operator=(const uint8_t& other);
            operator uint8_t();
        } msaa;


        // DeclareProperty(KRenderState, uint8_t, msaa_line);
        struct _msaa_line
        {
            _msaa_line& operator=(const uint8_t& other);
            operator uint8_t();
        } msaa_line;


        // DeclareProperty(KRenderState, uint8_t, sampleShadingEnable);
        struct _sampleShadingEnable
        {
            _sampleShadingEnable& operator=(const uint8_t& other);
            operator uint8_t();
        } sampleShadingEnable;


        // DeclareProperty(KRenderState, enumSampleCountFlag, sampleCountFlag);
        struct _sampleCountFlag
        {
            _sampleCountFlag& operator=(const enumSampleCountFlag& other);
            operator enumSampleCountFlag();
        } sampleCountFlag;


        // DeclareProperty(KRenderState, uint8_t, sampleAlphaToCoverageEnable);
        struct _sampleAlphaToCoverageEnable
        {
            _sampleAlphaToCoverageEnable& operator=(const uint8_t& other);
            operator uint8_t();
        } sampleAlphaToCoverageEnable;


        // DeclareProperty(KRenderState, uint8_t, sampleAlphaToOneEnable);
        struct _sampleAlphaToOneEnable
        {
            _sampleAlphaToOneEnable& operator=(const uint8_t& other);
            operator uint8_t();
        } sampleAlphaToOneEnable;


        // DeclareProperty(KRenderState, uint32_t, sampleMask);
        struct _sampleMask
        {
            _sampleMask& operator=(const uint32_t& other);
            operator uint32_t();
        } sampleMask;

        // DeclareProperty(KRenderState, float, minSampleShading);
        struct _minSampleShading
        {
            _minSampleShading& operator=(const float& other);
            operator float();
        } minSampleShading;


        // DeclareProperty(KRenderState, enumPolygonMode, polygonMode);
        struct _polygonMode
        {
            _polygonMode& operator=(const enumPolygonMode& other);
            operator enumPolygonMode();
        } polygonMode;


        // DeclareProperty(KRenderState, enumFrontFaceMode, frontFaceMode);
        struct _frontFaceMode
        {
            _frontFaceMode& operator=(const enumFrontFaceMode& other);
            operator enumFrontFaceMode();
        } frontFaceMode;

        // DeclareProperty(KRenderState, enumCullMode, cullMode);
        struct _cullMode
        {
            _cullMode& operator=(const enumCullMode& other);
            operator enumCullMode();
        } cullMode;

        // DeclareProperty(KRenderState, enumDrawMode, drawMode);
        struct _drawMode
        {
            _drawMode& operator=(const enumDrawMode& other);
            operator enumDrawMode();
        } drawMode;

        // DeclareProperty(KRenderState, uint8_t, depthBiasEnable);
        struct _depthBiasEnable
        {
            _depthBiasEnable& operator=(const uint8_t& other);
            operator uint8_t();
        } depthBiasEnable;

        // DeclareProperty(KRenderState, uint8_t, depthClampEnable);
        struct _depthClampEnable
        {
            _depthClampEnable& operator=(const uint8_t& other);
            operator uint8_t();
        } depthClampEnable;


        // DeclareProperty(KRenderState, uint8_t, rasterizerDiscardEnable);
        struct _rasterizerDiscardEnable
        {
            _rasterizerDiscardEnable& operator=(const uint8_t& other);
            operator uint8_t();
        } rasterizerDiscardEnable;

        // DeclareProperty(KRenderState, uint8_t, blendAttachCount);
        struct _blendAttachCount
        {
            _blendAttachCount& operator=(const uint8_t& other);
            operator uint8_t();
        } blendAttachCount;

        struct _mrtMask
        {
            _mrtMask& operator=(const uint8_t& other);
            operator uint8_t();
        } mrtMask;


        // DeclareProperty(KRenderState, uint8_t, alphaRef);
        struct _alphaRef
        {
            _alphaRef& operator=(const uint8_t& other);
            operator uint8_t();
        } alphaRef;

        // DeclareProperty(KRenderState, uint8_t, pointSize);
        struct _pointSize
        {
            _pointSize& operator=(const uint8_t& other);
            operator uint8_t();
        } pointSize;

        // DeclareProperty(KRenderState, uint8_t, depthTestEnable);
        struct _depthTestEnable
        {
            _depthTestEnable& operator=(const uint8_t& other);
            operator uint8_t();
        } depthTestEnable;

        // DeclareProperty(KRenderState, uint8_t, depthWriteEnable);
        struct _depthWriteEnable
        {
            _depthWriteEnable& operator=(const uint8_t& other);
            operator uint8_t();
        } depthWriteEnable;

        // DeclareProperty(KRenderState, enumDepthType, depthCompareOp);
        struct _depthCompareOp
        {
            _depthCompareOp& operator=(const enumDepthType& other);
            operator enumDepthType();
        } depthCompareOp;

        // DeclareProperty(KRenderState, uint8_t, depthBoundsTestEnable);
        struct _depthBoundsTestEnable
        {
            _depthBoundsTestEnable& operator=(const uint8_t& other);
            operator uint8_t();
        } depthBoundsTestEnable;


        // DeclareProperty(KRenderState, uint8_t, stencilTestEnable);
        struct _stencilTestEnable
        {
            _stencilTestEnable& operator=(const uint8_t& other);
            operator uint8_t();
        } stencilTestEnable;


        struct _stencilFront
        {
            // DeclareSecondProperty(KRenderState, uint32_t, stencilFront, compareMask);
            struct _compareMask
            {
                _compareMask& operator=(const uint32_t& other);
                operator uint32_t();
            } compareMask;

            // DeclareSecondProperty(KRenderState, uint32_t, stencilFront, writeMask);
            struct _writeMask
            {
                _writeMask& operator=(const uint32_t& other);
                operator uint32_t();
            } writeMask;

            // DeclareSecondProperty(KRenderState, short, stencilFront, reference);
            struct _reference
            {
                _reference& operator=(const short& other);
                operator short();
            } reference;

            // DeclareSecondProperty(KRenderState, enumStencilType, stencilFront, stencilCompareOp);
            struct _stencilCompareOp
            {
                _stencilCompareOp& operator=(const enumStencilType& other);
                operator enumStencilType();
            } stencilCompareOp;

            // DeclareSecondProperty(KRenderState, enumStencilOpType, stencilFront, sencilFailOp);
            struct _sencilFailOp
            {
                _sencilFailOp& operator=(const enumStencilOpType& other);
                operator enumStencilOpType();
            } sencilFailOp;

            // DeclareSecondProperty(KRenderState, enumStencilOpType, stencilFront, stencilPassOp);
            struct _stencilPassOp
            {
                _stencilPassOp& operator=(const enumStencilOpType& other);
                operator enumStencilOpType();
            } stencilPassOp;

            // DeclareSecondProperty(KRenderState, enumStencilOpType, stencilFront, stencilDepthFailOp);
            struct _stencilDepthFailOp
            {
                _stencilDepthFailOp& operator=(const enumStencilOpType& other);
                operator enumStencilOpType();
            } stencilDepthFailOp;

        } stencilFront;

        struct _stencilBack
        {
            // DeclareSecondProperty(KRenderState, uint32_t, stencilBack, compareMask);
            struct _compareMask
            {
                _compareMask& operator=(const uint32_t& other);
                operator uint32_t();
            } compareMask;


            // DeclareSecondProperty(KRenderState, uint32_t, stencilBack, writeMask);
            struct _writeMask
            {
                _writeMask& operator=(const uint32_t& other);
                operator uint32_t();
            } writeMask;

            // DeclareSecondProperty(KRenderState, short, stencilBack, reference);
            struct _reference
            {
                _reference& operator=(const short& other);
                operator short();
            } reference;

            // DeclareSecondProperty(KRenderState, enumStencilType, stencilBack, stencilCompareOp);
            struct _stencilCompareOp
            {
                _stencilCompareOp& operator=(const enumStencilType& other);
                operator enumStencilType();
            } stencilCompareOp;

            // DeclareSecondProperty(KRenderState, enumStencilOpType, stencilBack, sencilFailOp);
            struct _sencilFailOp
            {
                _sencilFailOp& operator=(const enumStencilOpType& other);
                operator enumStencilOpType();
            } sencilFailOp;

            // DeclareSecondProperty(KRenderState, enumStencilOpType, stencilBack, stencilPassOp);
            struct _stencilPassOp
            {
                _stencilPassOp& operator=(const enumStencilOpType& other);
                operator enumStencilOpType();
            } stencilPassOp;

            // DeclareSecondProperty(KRenderState, enumStencilOpType, stencilBack, stencilDepthFailOp);
            struct _stencilDepthFailOp
            {
                _stencilDepthFailOp& operator=(const enumStencilOpType& other);
                operator enumStencilOpType();
            } stencilDepthFailOp;

        } stencilBack;


        struct _blendAttachment
        {
            struct _item
            {
                uint32_t id;
                _item();
                _item(uint32_t i);

                // DeclareArrayItemProperty(KRenderState, uint8_t, blendAttachment, blendEnable);
                struct _blendEnable
                {
                    _blendEnable& operator=(const uint8_t& other);
                    operator uint8_t();
                } blendEnable;

                // DeclareArrayItemProperty(KRenderState, uint8_t, blendAttachment, writeR);
                struct _writeR
                {
                    _writeR& operator=(const uint8_t& other);
                    operator uint8_t();
                } writeR;

                // DeclareArrayItemProperty(KRenderState, uint8_t, blendAttachment, writeG);
                struct _writeG
                {
                    _writeG& operator=(const uint8_t& other);
                    operator uint8_t();
                } writeG;

                // DeclareArrayItemProperty(KRenderState, uint8_t, blendAttachment, writeB);
                struct _writeB
                {
                    _writeB& operator=(const uint8_t& other);
                    operator uint8_t();
                } writeB;

                // DeclareArrayItemProperty(KRenderState, uint8_t, blendAttachment, writeA);
                struct _writeA
                {
                    _writeA& operator=(const uint8_t& other);
                    operator uint8_t();
                } writeA;


                // DeclareArrayItemProperty(KRenderState, enumBlendType, blendAttachment, srcColorBlendFactor);
                struct _srcColorBlendFactor
                {
                    _srcColorBlendFactor& operator=(const enumBlendType& other);
                    operator enumBlendType();
                } srcColorBlendFactor;

                // DeclareArrayItemProperty(KRenderState, enumBlendType, blendAttachment, dstColorBlendFactor);
                struct _dstColorBlendFactor
                {
                    _dstColorBlendFactor& operator=(const enumBlendType& other);
                    operator enumBlendType();
                } dstColorBlendFactor;

                // DeclareArrayItemProperty(KRenderState, enumBlendType, blendAttachment, srcAlphaBlendFactor);
                struct _srcAlphaBlendFactor
                {
                    _srcAlphaBlendFactor& operator=(const enumBlendType& other);
                    operator enumBlendType();
                } srcAlphaBlendFactor;

                // DeclareArrayItemProperty(KRenderState, enumBlendType, blendAttachment, dstAlphaBlendFactor);
                struct _dstAlphaBlendFactor
                {
                    _dstAlphaBlendFactor& operator=(const enumBlendType& other);
                    operator enumBlendType();
                } dstAlphaBlendFactor;

                // DeclareArrayItemProperty(KRenderState, enumBlendEquationType, blendAttachment, colorBlendOp);
                struct _colorBlendOp
                {
                    _colorBlendOp& operator=(const enumBlendEquationType& other);
                    operator enumBlendEquationType();
                } colorBlendOp;

                // DeclareArrayItemProperty(KRenderState, enumBlendEquationType, blendAttachment, alphaBlendOp);
                struct _alphaBlendOp
                {
                    _alphaBlendOp& operator=(const enumBlendEquationType& other);
                    operator enumBlendEquationType();
                } alphaBlendOp;

            } item[KMAX_BLEND_ATTACHMENT];

            _item& operator[](int i);

            _blendAttachment();

        } blendAttachment;


        // DeclareProperty(KRenderState, uint8_t, defaultViewPortEnable);
        struct _defaultViewPortEnable
        {
            _defaultViewPortEnable& operator=(const uint8_t& other);
            operator uint8_t();
        } defaultViewPortEnable;

        // DeclareProperty(KRenderState, uint8_t, defaultScissorEnable);
        struct _defaultScissorEnable
        {
            _defaultScissorEnable& operator=(const uint8_t& other);
            operator uint8_t();
        } defaultScissorEnable;

    public:
        KRenderState();

        KRenderState& operator=(const gfx::KRenderState& other);

        int operator==(const gfx::KRenderState& other) const;

        int operator!=(const gfx::KRenderState& other) const;

        void ResetDefaultValue();

        uint32_t GetHash() const;

        int SaveToMetaSection(KMetaSection* pSection);
        int LoadFromMetaSction(KMetaSection* pSection);
    };

    class IKSharedPreBinder
    {
    public:
        virtual IKSharedPreBinder& BeginPreBind(BOOL bForce = FALSE) = 0;
        virtual IKSharedPreBinder& PreBindSampler(const_pool_str pName, gfx::IKGFX_Sampler* pSampler) = 0;
        //virtual IKSharedPreBinder& PreBindBuffer(const_pool_str pName, gfx::IKGFX_Buffer* pBuffer) = 0;
        virtual IKSharedPreBinder& PreBindTexture(const_pool_str pName, gfx::IKGFX_TextureView* pTexture) = 0;
        virtual IKSharedPreBinder& PreBindTextures(const_pool_str pName, uint32_t uCount, gfx::IKGFX_TextureView* pTexture[]) = 0;
        virtual IKSharedPreBinder& PreBindBufferView(const_pool_str pName, gfx::IKGFX_BufferView* pBufView) = 0;
        virtual BOOL               EndPreBind() = 0;
        virtual uint64_t           GetHash() const = 0;
    };




    //Ray Tracing Part
#define RAYTRACING_COMMON_PARAM_BINDLESS_CB_NAME    "CBuffer_CommonBindlessID"
#define RAYTRACING_LOCAL_MATERIAL_BINDLESS_CB_NAME  "CBuffer_LocalMaterialBindlessID"
#define RAYTRACING_LOCAL_ENGINE_BINDLESS_CB_NAME    "CBuffer_LocalEngineBindlessID"
#define RAYTRACING_LOCAL_MATERIAL_PARAM_CB_NAME     "MaterialLocalParams"
#define RAYTRACING_BINDLESS_BINDING_SPACE 0
#define RAYTRACING_GLOBAL_BINDING_SPACE 1
#define RAYTRACING_LOCALL_BINDING_SPACE 2
    constexpr uint32_t K_MAX_RT_SHADER_STAGES = 8192;

    enum class enumAccelerationStructureBuildMode
    {
        // Perform a full acceleration structure build.
        KRT_BM_BUILD,

        // Update existing acceleration structure, based on new vertex positions.
        // Index buffer must not change between initial build and update operations.
        KRT_BM_UPDATE,
        KRT_BM_MAX_ENUM
    };

    enum enumRayTracingGeometryType
    {
        // 传统三角形网格
        KRT_GT_TRIANGLE,

        // 程序化生成的光追对象，要自定义intersection求交shader.
        // 不传IB
        KRT_GT_PROCEDURAL,
        KRT_GT_MAX_ENUM
    };
    /// <summary>
    /// 这里单独搞一个是为了防止传别的进来
    /// </summary>
    enum enumRayTracingShaderType
    {
        KRT_ST_RAY_GEN,
        KRT_ST_HIT_GROUP,
        KRT_ST_MISS,
        KRT_ST_CALLABLE,
        KRT_ST_MAX_ENUM
    };

    enum ERTShaderSubType
    {
        E_RT_TYPE_DEFAULT,
        E_RT_TYPE_CLOSEST_HIT = E_RT_TYPE_DEFAULT,
        E_RT_TYPE_ANY_HIT,
        E_RT_TYPE_INTERSECTION,
    };

    typedef struct KRayTracingShaderSubmoduleDesc
    {
        const char* szMainShaderPath = nullptr;
        const NSKBase::tagFileLocation* userShaderFileLoc = nullptr;
        const char* szEntryPoint = nullptr;
        const char* szMacroDefine = nullptr;
        ERTShaderSubType sType = ERTShaderSubType::E_RT_TYPE_DEFAULT;
    }KRayTracingShaderSubmoduleDesc;
    typedef struct KRayTracingShaderCreateDesc
    {
        std::vector<KRayTracingShaderSubmoduleDesc> vecSubmodule;
        enumRayTracingShaderType sType = enumRayTracingShaderType::KRT_ST_MAX_ENUM;
        uint64_t inHash = UINT64_MAX;
    }KRayTracingShaderCreateDesc;

    struct KRayTracingUniformInfo
    {
        uint32_t                 m_uNameHash = 0;
        const_pool_str           m_szBlockName = nullptr;
        const_pool_str           m_szName = nullptr;
        gfx::enumUniformType     m_UniformType = (gfx::enumUniformType)0;
        gfx::enumUniformBaseType m_UniformBaseType = (gfx::enumUniformBaseType)0;

        uint8_t  m_uVectorSize = 0;
        uint8_t  m_uMatcol = 0;
        uint8_t  m_uMatrow = 0;
        uint16_t m_uArrayCount = 0;
        uint32_t m_uByteSize = 0;
        uint16_t m_nOffset = 0;
    };
    struct KRayTracingUniformBlockInfo : public KGfxRef
    {
        const_pool_str m_szName = nullptr;
        gfx::enumUniformType  m_UniformType{};
        uint32_t m_nBinding = 0;
        uint32_t m_nSpace = 0;
        uint32_t m_block16bytesAlignMemoryForGpu = 0;
        std::unordered_map<const_pool_str, KRayTracingUniformInfo*> m_mapUnifroms;
        KRayTracingUniformInfo* GetUnifromByName(const char* name);
        //for textures
        uint32_t             m_uArrayCount = 0;
        TextureType          m_eTextureType = TextureType::Count;
        ~KRayTracingUniformBlockInfo();
    };
    typedef struct IRayTracingShader
    {
        IRayTracingShader() = default;
        virtual ~IRayTracingShader() {};
        virtual bool Create(const KRayTracingShaderCreateDesc& ci) = 0;
        enumRayTracingShaderType eShaderType = enumRayTracingShaderType::KRT_ST_MAX_ENUM;
        virtual auto             GetHash() -> uint64_t = 0;
        virtual auto             GetType() const -> enumRayTracingShaderType = 0;
        virtual KRayTracingUniformBlockInfo* GetLocalMaterialBindlessIDUniformBlockInfo() = 0;
        virtual KRayTracingUniformBlockInfo* GetLocalEngineBindlessIDUniformBlockInfo() = 0;
        virtual KRayTracingUniformBlockInfo* GetLocalMaterialParamUniformBlockInfo() = 0;

        //call only on raygen shader
        virtual KRayTracingUniformBlockInfo* GetCommonUniformBlockInfo() = 0;
    } IRayTracingShader;

    typedef struct RayTracingProgramDesc
    {
        std::vector<IRayTracingShader*> vecRayGenShaders;
        std::vector<IRayTracingShader*> vecMissShaders;
        std::vector<IRayTracingShader*> vecHitShaders;
        std::vector<IRayTracingShader*> vecCallableShaders;
        uint32_t                    uMaxPayloadSize = 0;
        uint32_t                    uMaxRayRecursionDepth = 0;       // raycast最大递归次数
    } RayTracingProgramDesc;
    class KRayTracingScene;
    class KRayTracingProgram
    {
    public:
        KRayTracingProgram() {};
        virtual ~KRayTracingProgram() {};
        virtual auto Create(const RayTracingProgramDesc& rtpDC) -> bool;
        virtual auto Destroy() -> void;

    public:
        uint32_t GetRayTracingHitGroupIndex(IRayTracingShader* pShader);
        uint32_t GetRayTracingMissShaderIndex(IRayTracingShader* pShader);
        uint32_t GetRayTracingCallableShaderIndex(IRayTracingShader* pShader);
        uint32_t GetRayTracingRayGenShaderIndex(IRayTracingShader* pShader);
        uint32_t GetRayTracingShaderIndexInPipeline(IRayTracingShader* pShader);
    public:
        virtual bool BeginBind(IKGFX_RenderContext* pRenderCtx) { return true; };
        virtual bool AddBindlessUAV(IKGFX_BufferView* pUAV) = 0;
        virtual bool AddBindlessUAV(IKGFX_TextureView* pTexView) = 0;
        virtual bool AddBindlessSRV(IKGFX_BufferView* pSRV) = 0;
        virtual bool AddBindlessSRV(IKGFX_TextureView* pTexView) = 0;
        virtual bool AddBindlessCBV(IKGFX_BufferView* pBufView) = 0;
        virtual bool AddBindlessSampler(IKGFX_Sampler* pSampler) = 0;
        virtual bool AddBindlessRayTracingScene(KRayTracingScene* pRTScene) = 0;
        virtual bool AddBindCBV(const_pool_str pName, IKGFX_BufferView* pCBV) = 0;

        virtual bool EndBind(IKGFX_RenderContext* pRenderCtx) = 0;
        //virtual bool Apply(IKGFX_RenderContext* pRenderCtx) = 0;
    private:
        std::unordered_map<uint64_t, uint32_t> m_mapHitGroupHashToIndex;
        std::unordered_map<uint64_t, uint32_t> m_mapCallableShaderHashToIndex;
        std::unordered_map<uint64_t, uint32_t> m_mapMissShaderHashToIndex;
        std::unordered_map<uint64_t, uint32_t> m_mapRayGenShaderHashToIndex;
    };


    typedef struct RayTracingGeomerySegment
    {
        IKGFX_Buffer* pVertexBuffer = nullptr; // VB
        enumVertexFormat eVertFormat = VERT_FORMAT_R32G32B32_SFLOAT;
        uint32_t         uVertexBufferOffset = 0;
        uint32_t         uVertexBufferStride = 12;      // float3
        uint32_t         uVertexCount = 0;
        // support triangle primitive only
        // the index of the first triangle of the segment in index buffer
        uint32_t         uFirstPrimitive = 0;
        // count of the triangles
        uint32_t         uNumPrimitive = 0;
        bool             bOpaque = FALSE;

    } RayTracingGeomerySegment;

    typedef struct RayTracingGeomeryCreateDesc
    {
        enumRayTracingGeometryType eGeometryType = KRT_GT_TRIANGLE;
        IKGFX_Buffer* pIndexBuffer = nullptr; //	IB
        uint32_t                   uIndexBufferOffset = 0;       //	IB offset
        enumIndexType              eIndexType = INDEX_TYPE_UINT32;
        RayTracingGeomerySegment* pSegments = nullptr; //  多个segment是为了不同部分响应不同材质，相当于submesh
        uint32_t                   uSegmentsCount = 0;       //	segment的数量，至少为1
        bool                       bAllowUpdate = FALSE;   //	True for dynamic model
    } RayTracingGeomeryCreateDesc;

    typedef struct RayTracingGeomeryUpdateParams
    {
        IKGFX_Buffer* pIndexBuffer = nullptr; //	IB
        uint32_t                   uIndexBufferOffset = 0;       //	IB offset
        RayTracingGeomerySegment* pSegments = nullptr; //  多个segment是为了不同部分响应不同材质，相当于submesh
        uint32_t                           uSegmentsCount = 0;       //	segment的数量，至少为1
        enumAccelerationStructureBuildMode eBuildMode = enumAccelerationStructureBuildMode::KRT_BM_BUILD;
    } RayTracingGeomeryBuildParams;


    class KRayTracingGeomery
    {
    public:
        KRayTracingGeomery() = default;
        virtual ~KRayTracingGeomery() {};
        /// <summary>
        /// 填充一个geometry对象,分配显存
        /// 是否更新的逻辑也在外面判断，这里只要给数据，和mode，就执行，不会再验证
        /// 这里同时计算出需要的buffer大小，可以拿之后方便外面优化到一个scratchbuffer上build
        /// </summary>
        /// <param name="gdc">构建参数</param>
        /// <param name="commandBuffer">用于创建的cb，空的话用upload command buffer（应该是用不上的）</param>
        /// <returns></returns>
        // virtual auto Create(const RayTracingGeomeryCreateDesc& gdc, IKGFX_RenderContext* commandBuffer = nullptr)->BOOL = 0;
        virtual auto Destroy() -> void = 0;
        /// <summary>
        /// 更新结构几何信息
        /// </summary>
        /// <param name="updateParam">需要的更新参数</param>
        /// <param name="commandBuffer">用于build的commandbuffer，空的话用uploadcommandbuffer</param>
        /// <returns></returns>
        // virtual auto Update(const RayTracingGeomeryUpdateParams& updateParam, IKGFX_RenderContext* commandBuffer = nullptr) -> BOOL = 0;

    protected:
        bool bDynamic = false;
    };

    typedef struct RayTracingSceneCreateDesc
    {
        uint32_t uMaxGeometryInstanceCount = 0; // decide the worst case size of the rt scene;
    } RayTracingSceneDesc;
    typedef struct RayTracingInstance
    {
        const NSKMath::KMatrix* pTransform = nullptr;
        uint32_t                uHitGroupOffset = 0;
        uint32_t                uCountShaderGroups = 0;       // count must match the segment count of the geo
        uint32_t                uInstanceID = UINT32_MAX;
        KRayTracingGeomery* pGeometry = nullptr; // the ref geometry
        bool bForceOpaque = false;  // => force all segment to apply GEOMETRY_OPAQUE_BIT_KHR 
    } RayTracingInstance;
    class KRayTracingScene;
    typedef struct RayTracingSceneUpdateParams
    {
        KRayTracingScene* pScene = nullptr;
        RayTracingInstance* pInstance = nullptr;
        uint32_t                           uInstanceCount = 0;
        enumAccelerationStructureBuildMode eBuildMode = enumAccelerationStructureBuildMode::KRT_BM_BUILD;
    } RayTracingSceneUpdateParams;
    // TLAS
    class KRayTracingScene
    {
    public:
        KRayTracingScene() = default;
        virtual ~KRayTracingScene() {};
        /// <summary>
        /// 创建一个TLAS，空的，计算大小而已
        /// </summary>
        /// <param name="RTCDC"></param>
        /// <param name="commandBuffer"></param>
        /// <returns></returns>
        // virtual auto Create(const RayTracingSceneCreateDesc& RTCDC, IKGFX_RenderContext* commandBuffer = nullptr)-> BOOL = 0;
        virtual auto Destroy() -> void = 0;
        virtual uint32_t GetBindlessHandle() = 0;
    };


    typedef struct HitShaderBindingTableDesc
    {
        uint32_t uInitHitGroupCount = 0;
        uint32_t uInitMissCount = 0;
        uint32_t uInitRayGenCount = 0;
        uint32_t uInitCallableCount = 0;
        uint32_t   uHitRecordAlignedSizeInByte = 0; // must be aligned with 8 && >= 16<= 512
        uint32_t   uRayGenRecordAlignedSizeInByte = 0; //  must be aligned with 8 && >= 16<= 512
        uint32_t   uMissRecordAlignedSizeInByte = 0; // must be aligned with 8 && >= 16<= 512
        uint32_t   uCallableRecordAlignedSizeInByte = 0; //  must be aligned with 8 && >= 16 <= 512
    } ShaderBindingTableDesc;

    typedef struct KRayTracingShaderBinding
    {
        uint32_t uRecordIndex = UINT32_MAX;
        uint32_t uShaderIndexInPipeline = UINT32_MAX;
        gfx::IKGFX_Buffer* pLocalVertexBuffer = nullptr;
        gfx::IKGFX_Buffer* pLocalIndexBuffer = nullptr;
        gfx::IKGFX_Buffer* pLocalMaterialBindlessIDCbuffer = nullptr;
        gfx::IKGFX_Buffer* pLocalEngineBindlessIDCbuffer = nullptr;
        gfx::IKGFX_Buffer* pLocalCustomInfoCbuffer = nullptr;
        gfx::IKGFX_Buffer* pLocalMaterialParamCbuffer = nullptr;
    }KRayTracingShaderBinding;
    class KShaderBindingTable
    {
    public:
        KShaderBindingTable() = default;
        virtual ~KShaderBindingTable() {};
        virtual auto Create(const ShaderBindingTableDesc& SBTDC) -> BOOL = 0;
        virtual auto Destroy() -> void = 0;
    public:
        //新接口
        virtual bool SetShaderBinding(const KRayTracingShaderBinding& binding, KRayTracingProgram* pipeline, enumRayTracingShaderType sType) = 0;
        virtual bool SetHitGroupBinding(const KRayTracingShaderBinding& binding, KRayTracingProgram* pipeline) = 0;
        virtual bool SetMissShaderBinding(const KRayTracingShaderBinding& binding, KRayTracingProgram* pipeline) = 0;
        virtual bool SetRayGenShaderBinding(const KRayTracingShaderBinding& binding, KRayTracingProgram* pipeline) = 0;
        virtual bool SetCallableShaderBinding(const KRayTracingShaderBinding& binding, KRayTracingProgram* pipeline) = 0;
        virtual bool CommitShaderBindingTable(IKGFX_RenderContext* commandBuffer) = 0;
    };


    typedef struct RayTracingGeometryUpdateBatch
    {
        KRayTracingGeomery** ppGeometries = nullptr;
        RayTracingGeomeryUpdateParams* pPerGeometryUpdateParams = nullptr;
        uint32_t                       uGeometryCount = 0;
        uint32_t                       uUpdateParamCount = 0;
    } RayTracingGeometryUpdateBatch;

    /// <summary>
    /// 只当个接口代理用，后面看看合到context里面就可以删掉了，但是感觉这种比较独立的功能不太适合合进去
    /// </summary>
    struct IKRayTracingProxy
    {
        virtual ~IKRayTracingProxy() {};
        virtual auto	CommitRayTracingProgram(const RayTracingProgramDesc& rtpDC) -> KRayTracingProgram* = 0;

        virtual auto    CreateRHIRayTracingGeomtry() -> KRayTracingGeomery* = 0;
        virtual auto	InitRHIRayTracingGeometry(const RayTracingGeomeryCreateDesc& createDesc, KRayTracingGeomery* pRHIGeometry) -> bool = 0;

        virtual auto	CommitRHIRayTracingGeometries(const RayTracingGeometryUpdateBatch& updateBatch, IKGFX_RenderContext* commandBuffer = nullptr) -> BOOL = 0;
        virtual auto	CreateRHIRayTracingScene(const RayTracingSceneCreateDesc& createDesc) -> KRayTracingScene* = 0;
        virtual auto	CommitRHIRayTracingScene(const RayTracingSceneUpdateParams& updateParam, IKGFX_RenderContext* commandBuffer = nullptr) -> BOOL = 0;
        virtual auto    CreateRHIShaderBindingTable(const ShaderBindingTableDesc& SBTDC, KRayTracingProgram* program) -> KShaderBindingTable* = 0;
        virtual auto	TraceRay(IRayTracingShader* pRayGenShader, IRayTracingShader* pMissShader, IRayTracingShader* pCallableShader, KRayTracingProgram* rayTracingProgram, KShaderBindingTable* shaderBindingTable,
            uint32_t width, uint32_t height, IKGFX_RenderContext* commandBuffer) -> bool = 0;
        virtual auto    CreateRayTracingShader(const KRayTracingShaderCreateDesc& ci) -> IRayTracingShader* = 0;
    };

    void InitDefaultDepthStencilFormat();
    enumTextureFormat GetDefaultDepthStencilFormat();
} // namespace gfx
