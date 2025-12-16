////////////////////////////////////////////////////////////////////////////////
//
//  FileName    : IGFX_Private.h
//  Creator     : HuaFei
//  Create Date : 2024-12
//
////////////////////////////////////////////////////////////////////////////////

// 这个文件是设计上：
// 1、包含KEnginePub内部的符号定义，不对外开放，只能在KEnginePub内部使用
// 2、只放置渲染api以上的通用定义，各渲染api相关的定义放在各自的头文件中

#pragma once

#include "KEnginePub/Public/IGFX_Public.h"
#include "KBase/Public/KBasePub.h"
#include "KBase/Public/math/KMathPublic.h"
#include "KBase/Public/io/KByteStream.h"
#include "KBase/Public/io/KMetaData.h"
#include "KEnginePub/Public/IKHeader.h"
#include "Engine/KUniqueString.h"
#include "KEnginePub/Public/IKTexture.h"
#include "IKShaderreflector.h"
#include <string>
#include <atomic>
#include <cstdint>
#include <unordered_map>
#include <mutex>

#ifdef _WIN32 // VK_USE_PLATFORM_WIN32_KHR
typedef struct HINSTANCE__* HINSTANCE;
typedef struct HWND__*      HWND;
#endif

#ifdef VK_USE_PLATFORM_WAYLAND_KHR
struct wl_display;
struct wl_surface;
#endif

#ifdef __ANDROID__ // VK_USE_PLATFORM_ANDROID_KHR
struct ANativeWindow;
#endif

#ifdef __OHOS__ // VK_USE_PLATFORM_OHOS
typedef struct NativeWindow OHNativeWindow;
#endif

#ifdef VK_USE_PLATFORM_XCB_KHR
struct xcb_connection_t;
typedef uint32_t xcb_window_t;
#endif

#define CONST_SCENEID    0

#define USER_SHADER_NAME "UserShader.fx5"


namespace gfx
{
    enum enuStencilMask : uint8_t
    {
        STENCIL_MASK_TERRAIN_CULL = 0x1,
        STENCIL_MASK_OCEAN_HOLE   = 0x2,
        STENCIL_MASK_ROLE         = 0x4,
        STENCIL_MASK_FORLIAGE     = 0x8,
        STENCIL_MASK_TEARRAIN     = 0x10,
        STENCIL_MASK_GRASS        = 0x20
    };

    enum enuStencilRef : uint8_t
    {
        STENCIL_REF_TERRAIN_CULL = 1,
        STENCIL_REF_OCEAN_HOLE   = 2,
        STENCIL_REF_ROLE         = 4,
        STENCIL_REF_FORLIAGE     = 8,
        STENCIL_REF_TEARRAIN     = 0x10,
        STENCIL_REF_GRASS        = 0x20
    };

    inline enumTextureFormat GetHDRColorRenderTargetFormat()
    {
        enum enumTextureFormat eColorRTformat = gfx::TEX_FORMAT_R16G16B16A16_SFLOAT;
        if (DrvOption::bSupportB10G11R11)
        {
            eColorRTformat = gfx::TEX_FORMAT_B10G11R11_UFLOAT_PACK32;
        }
        return eColorRTformat;
    }

    struct KBindingTextures
    {
        std::vector<gfx::IKGFX_TextureView*> vecTextures;
    };

    class KSharedPreBinder : public IKSharedPreBinder
    {
    public:
        // Inherited via IKSharedPreBinder
        IKSharedPreBinder& BeginPreBind(BOOL bForce = FALSE) override;
        IKSharedPreBinder& PreBindSampler(const_pool_str pName, gfx::IKGFX_Sampler* pSampler) override;
        //IKSharedPreBinder& PreBindBuffer(const_pool_str pName, gfx::IKGFX_Buffer* pBuffer) override;
        IKSharedPreBinder& PreBindTexture(const_pool_str pName, gfx::IKGFX_TextureView* pTexture) override;
        IKSharedPreBinder& PreBindTextures(const_pool_str pName, uint32_t uCount, gfx::IKGFX_TextureView* pTexture[]) override;
        IKSharedPreBinder& PreBindBufferView(const_pool_str pName, gfx::IKGFX_BufferView* pBufView) override;

        gfx::IKGFX_TextureView*                                     GetPreBindTexture(const_pool_str pName);
        //gfx::IKGFX_Buffer*                                            GetPreBindBuffer(const_pool_str pName);
        gfx::KBindingTextures*                                      GetPreBindTextures(const_pool_str pName);
        gfx::IKGFX_BufferView*                                      GetPreBindBufferView(const_pool_str pName);
        gfx::IKGFX_Sampler*                                         GetPreBindSampler(const_pool_str pName);
        BOOL                                                        EndPreBind() override;
        uint64_t                                                    GetHash() const override;
        BOOL                                                        IsBeginBind() { return m_bBegin; }
        //std::unordered_map<const_pool_str, gfx::IKGFX_Buffer*>        m_mapPreBindBuffer;
        std::unordered_map<const_pool_str, gfx::IKGFX_TextureView*> m_mapPreBindTexture;
        std::unordered_map<const_pool_str, KBindingTextures>        m_mapPreBindTextures;
        std::unordered_map<const_pool_str, gfx::IKGFX_BufferView*>  m_mapPreBindBufferResourceView;
        std::unordered_map<const_pool_str, gfx::IKGFX_Sampler*>     m_mapPreBindSampler;

    private:
        BOOL     m_bBegin             = false;
        int32_t  m_nFrameCount        = 0;
        uint64_t m_uPreBindBufferHash = 0;
    };

    GFX_API GetGfxApi();

    BOOL InitRenderSystem(RenderSystemInfo& renderSysteInfo);
    void UninitRenderSystem();

    namespace RayTracingHelper
    {
        gfx::ShaderStageType GetGfxShaderStageTypeFromSubShaderType(ERTShaderSubType subType);
        gfx::ShaderStageType GetGfxShaderStageTypeFromRTShaderType(enumRayTracingShaderType sShaderType,ERTShaderSubType subType);
    }
}; // namespace gfx
