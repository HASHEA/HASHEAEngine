#include "KEnginePub/Public/IKTexture.h"
#include "IGFX_Private.h"
#include "KEngine/Public/IKEngineCore.h"
#include "KEngine/Public/KEngineCore.h"
#include "Engine/KGLog.h"

//////////////////////////////////////////////////
#include "vulkan/KGFX_GraphicDeviceVK.h"
#include "dx12/KGFX_GraphiceDeviceDX12.h"
//////////////////////////////////////////////////

#undef FreeMemory

#include "KEngine/Public/Render/KEngineRender.h"
#include "KBase/Public/async_task/KAsyncTaskManager.h"
#include "KBase/Public/thread/KThread.h"

//////////////////////////////////////////////////
#include "optick.h"
#include "KBase/Public/KMemLeak.h"

using namespace gfx;

gfx::IKGFX_GraphicDevice* g_pKGfxGraphicsDevice = nullptr;
void                      KGFX_InitDevice()
{
    if (!g_pKGfxGraphicsDevice)
    {
        if (DrvOption::GetRenderApi() == GFX_API::GFX_VULKAN_API)
        {
            g_pKGfxGraphicsDevice = new gfx::KGFX_GraphicDeviceVK();
        }
        else if (DrvOption::GetRenderApi() == GFX_API::GFX_DX12_API)
        {
#ifdef _WIN32
            g_pKGfxGraphicsDevice = new gfx::KGFX_GraphicDeviceDx12();
#endif
        }
    }
}

void KGFX_UninitDevice()
{
    if (g_pKGfxGraphicsDevice)
    {
        g_pKGfxGraphicsDevice->UnInit();
    }
    SAFE_DELETE(g_pKGfxGraphicsDevice);
}

namespace gfx
{
    void InitVertexDesc();

    gfx::IKGFX_GraphicDevice* KGFX_GetGraphicDevice()
    {
        return g_pKGfxGraphicsDevice;
    }

    IKGFX_RenderContext* GetRenderContext()
    {
        auto pDevice = KGFX_GetGraphicDevice();
        ASSERT(pDevice);
        return pDevice->GetRenderContext();
    }

    //BOOL KGFX_CreateContext(ESContext* pEsContext, enumGraphicContext eGraphicContext)
    //{
    //    BOOL                   bRet        = false;
    //    BOOL                   bRetCode    = false;
    //    NSEngine::KEngineCore* pEngineCore = nullptr;
    //    IKRender*              pRender     = nullptr;
    //    gfx::KWindow           window;
    //    window.m_uWidth           = pEsContext->width;
    //    window.m_uHeight          = pEsContext->height;
    //    window.m_uSwapChainWidth  = pEsContext->swapchainWidth;
    //    window.m_uSwapChainHeight = pEsContext->swapchainHeight;

    //    strncpy(window.m_szWindowName, "x3d_client_window", 128);
    //    window.m_uId = eGraphicContext;
    //    window.SetWindow(pEsContext->eglNativeWindow);

    //    pRender = NSRender::g_pEngineRender->GetRender();
    //    KGLOG_ASSERT_EXIT(pRender);

    //    bRetCode = pRender->Setup(&window);
    //    KGLOG_ASSERT_EXIT(bRetCode);

    //    bRet = true;
    //Exit0:
    //    return bRet;
    //}

    BOOL KGFX_CreateContext(const NSEngine::KWindowInfo& WindowInfo, enumGraphicContext eGraphicContext)
    {
        BOOL                      bResult = FALSE;
        BOOL                      bRetCode = FALSE;
        gfx::IKGFX_GraphicDevice* pKGFXGraphicDevice = nullptr;
        gfx::IKGFX_Swapchain* pContext = nullptr;
        gfx::KWindow              window;
        window.m_uWidth = WindowInfo.nWidth;
        window.m_uHeight = WindowInfo.nHeight;
        window.m_uRenderWidth = (uint32_t)WindowInfo.nRenderWidth;
        window.m_uRenderHeight = (uint32_t)WindowInfo.nRenderHeight;

        strncpy(window.m_szWindowName, "x3d_client_window", 128);
        window.m_uId = eGraphicContext;
        window.SetWindow(WindowInfo.pEglNativeWindow);

        pKGFXGraphicDevice = gfx::KGFX_GetGraphicDevice();
        ASSERT(pKGFXGraphicDevice);

        pKGFXGraphicDevice->Setup(&window);

        pContext = pKGFXGraphicDevice->GetContext(window.m_uId);
        KGLOG_PROCESS_ERROR(pContext);

        pContext->m_nRenderBufferWidth = WindowInfo.nRenderWidth;
        pContext->m_nRenderBufferHeight = WindowInfo.nRenderHeight;

        bResult = TRUE;
    Exit0:
        KGLogPrintf(KGLOG_INFO, "Setup window width:%d, height:%d %s", window.m_uWidth, window.m_uHeight, bResult ? "Succeeded" : "Failed");
        return bResult;
    }

    BOOL gfx::KGFX_InitRenderSystem(gfx::RenderSystemInfo& renderSysteInfo)
    {
        KGFX_InitDevice();
        return gfx::InitRenderSystem(renderSysteInfo);
    }

    void gfx::KGFX_UninitRenderSystem()
    {
        KGFX_UninitDevice();
        gfx::UninitRenderSystem();
    }

    BOOL InitRenderSystem(RenderSystemInfo& renderSysteInfo)
    {
        BOOL bRet = false;
        BOOL bRetCode = false;

        CHECK_ASSERT(g_pKGfxGraphicsDevice);

        bRetCode = g_pKGfxGraphicsDevice->Init(renderSysteInfo);
        KGLOG_PROCESS_ERROR(bRetCode);

        {
            auto TexturePool = NSEngine::GetTexturePool();
            CHECK_ASSERT(TexturePool);

            // 初始化基础资源
            TexturePool->GetBlankTexture();
            TexturePool->GetBlackTexture();
            TexturePool->GetDefaultShadowTex();
            TexturePool->GetErrorTexture();
            TexturePool->GetDefaultNormalTexture();
            TexturePool->GetErrorTextureArray();
            TexturePool->GetErrorTextureCube();
        }

        InitVertexDesc();

        bRet = true;
    Exit0:
        return bRet;
    }

    void UninitRenderSystem()
    {

    }

    GFX_API GetGfxApi()
    {
        return DrvOption::GetRenderApi();
    }

    /////////////////////////////////////////////////////////////////////////
    uint8_t baseAtt[] = {
        1, // Uint8
        2, // Uint16
        4, // Uint32
        2, // Half
        4, // Float
    };

    ////////////////////////////////////////////////////////////////////
    KVertexDecl::KVertexDecl(enumVertexDecl declType)
    {
        m_declType = declType;
        m_stride = 0;
        m_nItem = 0;

        for (uint32_t i = 0; i < gfx::KAttribUsage::COUNT; ++i)
        {
            m_ItemTable[i] = gfx::KAttribUsage::VERT_POS_INDX;
        }
    }


    const char* GetCommVertName(KAttribUsage::Enum type)
    {
        static_const_param_name _VERTEX_POS_INDX = GetParamNameByPool("VERTEX_POS_INDX");
        static_const_param_name _VERTEX_NORMAL_INDX = GetParamNameByPool("VERTEX_NORMAL_INDX");
        static_const_param_name _VERTEX_COLOR_INDX = GetParamNameByPool("VERTEX_COLOR_INDX");
        static_const_param_name _VERTEX_TANGENT_INDX = GetParamNameByPool("VERTEX_TANGENT_INDX");
        static_const_param_name _VERTEX_TEX0_INDX = GetParamNameByPool("VERTEX_TEX0_INDX");
        static_const_param_name _VERTEX_TEX1_INDX = GetParamNameByPool("VERTEX_TEX1_INDX");
        static_const_param_name _VERTEX_TEX2_INDX = GetParamNameByPool("VERTEX_TEX2_INDX");
        static_const_param_name _VERTEX_TEX3_INDX = GetParamNameByPool("VERTEX_TEX3_INDX");
        static_const_param_name _VERTEX_TEX4_INDX = GetParamNameByPool("VERTEX_TEX4_INDX");
        static_const_param_name _VERTEX_TEX5_INDX = GetParamNameByPool("VERTEX_TEX5_INDX");
        static_const_param_name _VERTEX_TEX6_INDX = GetParamNameByPool("VERTEX_TEX6_INDX");
        static_const_param_name _VERTEX_TEX7_INDX = GetParamNameByPool("VERTEX_TEX7_INDX");
        static_const_param_name _VERTEX_TEX8_INDX = GetParamNameByPool("VERTEX_TEX8_INDX");
        static_const_param_name _VERTEX_TEX9_INDX = GetParamNameByPool("VERTEX_TEX9_INDX");
        static_const_param_name _VERTEX_TEX10_INDX = GetParamNameByPool("VERTEX_TEX10_INDX");
        static_const_param_name _VERTEX_TEX11_INDX = GetParamNameByPool("VERTEX_TEX11_INDX");


        const char* pName = nullptr;
        switch (type)
        {
        case gfx::KAttribUsage::VERT_POS_INDX:
            pName = _VERTEX_POS_INDX;
            break;
        case gfx::KAttribUsage::VERT_NORMAL_INDX:
            pName = _VERTEX_NORMAL_INDX;
            break;
        case gfx::KAttribUsage::VERT_COLOR_INDX:
            pName = _VERTEX_COLOR_INDX;
            break;
        case gfx::KAttribUsage::VERT_TANGENT_INDX:
            pName = _VERTEX_TANGENT_INDX;
            break;
        case gfx::KAttribUsage::VERT_TEX0_INDX:
            pName = _VERTEX_TEX0_INDX;
            break;
        case gfx::KAttribUsage::VERT_TEX1_INDX:
            pName = _VERTEX_TEX1_INDX;
            break;
        case gfx::KAttribUsage::VERT_TEX2_INDX:
            pName = _VERTEX_TEX2_INDX;
            break;
        case gfx::KAttribUsage::VERT_TEX3_INDX:
            pName = _VERTEX_TEX3_INDX;
            break;
        case gfx::KAttribUsage::VERT_TEX4_INDX:
            pName = _VERTEX_TEX4_INDX;
            break;
        case gfx::KAttribUsage::VERT_TEX5_INDX:
            pName = _VERTEX_TEX5_INDX;
            break;
        case gfx::KAttribUsage::VERT_TEX6_INDX:
            pName = _VERTEX_TEX6_INDX;
            break;
        case gfx::KAttribUsage::VERT_TEX7_INDX:
            pName = _VERTEX_TEX7_INDX;
            break;
        case gfx::KAttribUsage::VERT_TEX9_INDX:
            pName = _VERTEX_TEX9_INDX;
            break;
        case gfx::KAttribUsage::VERT_TEX10_INDX:
            pName = _VERTEX_TEX10_INDX;
            break;
        case gfx::KAttribUsage::VERT_TEX11_INDX:
            pName = _VERTEX_TEX11_INDX;
            break;
        default:
            ASSERT(0);
        }
        return pName;
    }


    const char* GetSkinVertName(KAttribUsage::Enum type)
    {
        static_const_param_name _VERTEX_BLEND_INDX0 = GetParamNameByPool("VERTEX_BLEND_INDX0");
        static_const_param_name _VERTEX_BLEND_INDX1 = GetParamNameByPool("VERTEX_BLEND_INDX1");
        const char* pName = nullptr;
        switch (type)
        {
        case gfx::KAttribUsage::VERT_BLEND_INDX0:
            pName = _VERTEX_BLEND_INDX0;
            break;
        case gfx::KAttribUsage::VERT_BLEND_INDX1:
            pName = _VERTEX_BLEND_INDX1;
            break;
        default:
            ASSERT(0);
        }
        return pName;
    }

    const char* GetInstanceVertName(KAttribUsage::Enum type)
    {
        static_const_param_name _VERTEX_MATRIX_ROW1_INDX_INSTANCE = GetParamNameByPool("VERTEX_MATRIX_ROW1_INDX_INSTANCE");
        static_const_param_name _VERTEX_MATRIX_ROW2_INDX_INSTANCE = GetParamNameByPool("VERTEX_MATRIX_ROW2_INDX_INSTANCE");
        static_const_param_name _VERTEX_MATRIX_ROW3_INDX_INSTANCE = GetParamNameByPool("VERTEX_MATRIX_ROW3_INDX_INSTANCE");
        static_const_param_name _VERTEX_MATRIX_ROW4_INDX_INSTANCE = GetParamNameByPool("VERTEX_MATRIX_ROW4_INDX_INSTANCE");
        static_const_param_name _VERTEX_MATRIX_ROW5_INDX_INSTANCE = GetParamNameByPool("VERTEX_MATRIX_ROW5_INDX_INSTANCE");
        static_const_param_name _VERTEX_POINT_LIGHT_INDX_INSTANCE = GetParamNameByPool("VERTEX_POINT_LIGHT_INDX_INSTANCE");
        const char* pName = nullptr;
        switch (type)
        {
        case gfx::KAttribUsage::VERT_MATRIX_ROW1_INDX_INSTANCE:
            pName = _VERTEX_MATRIX_ROW1_INDX_INSTANCE;
            break;
        case gfx::KAttribUsage::VERT_MATRIX_ROW2_INDX_INSTANCE:
            pName = _VERTEX_MATRIX_ROW2_INDX_INSTANCE;
            break;
        case gfx::KAttribUsage::VERT_MATRIX_ROW3_INDX_INSTANCE:
            pName = _VERTEX_MATRIX_ROW3_INDX_INSTANCE;
            break;
        case gfx::KAttribUsage::VERT_MATRIX_ROW4_INDX_INSTANCE:
            pName = _VERTEX_MATRIX_ROW4_INDX_INSTANCE;
            break;
        case gfx::KAttribUsage::VERT_MATRIX_ROW5_INDX_INSTANCE:
            pName = _VERTEX_MATRIX_ROW5_INDX_INSTANCE;
            break;
        case gfx::KAttribUsage::VERT_POINT_LIGHT_INDX_INSTANCE:
            pName = _VERTEX_POINT_LIGHT_INDX_INSTANCE;
            break;
        default:
            ASSERT(0);
        }
        return pName;
    }

    gfx::KAttribUsage::Enum GetKAttribUsage(const_pool_str pName)
    {
        using E = gfx::KAttribUsage::Enum;

        static const auto& kTable = []() -> const std::unordered_map<const_pool_str, E>&
            {
                static std::unordered_map<const_pool_str, E> m{
                    { GetParamNameByPool("VERTEX_POS_INDX"),                 E::VERT_POS_INDX },
                    { GetParamNameByPool("VERTEX_NORMAL_INDX"),              E::VERT_NORMAL_INDX },
                    { GetParamNameByPool("VERTEX_COLOR_INDX"),               E::VERT_COLOR_INDX },
                    { GetParamNameByPool("VERTEX_TANGENT_INDX"),             E::VERT_TANGENT_INDX },

                    { GetParamNameByPool("VERTEX_TEX0_INDX"),                E::VERT_TEX0_INDX },
                    { GetParamNameByPool("VERTEX_TEX1_INDX"),                E::VERT_TEX1_INDX },
                    { GetParamNameByPool("VERTEX_TEX2_INDX"),                E::VERT_TEX2_INDX },
                    { GetParamNameByPool("VERTEX_TEX3_INDX"),                E::VERT_TEX3_INDX },
                    { GetParamNameByPool("VERTEX_TEX4_INDX"),                E::VERT_TEX4_INDX },
                    { GetParamNameByPool("VERTEX_TEX5_INDX"),                E::VERT_TEX5_INDX },
                    { GetParamNameByPool("VERTEX_TEX6_INDX"),                E::VERT_TEX6_INDX },
                    { GetParamNameByPool("VERTEX_TEX7_INDX"),                E::VERT_TEX7_INDX },
                    { GetParamNameByPool("VERTEX_TEX8_INDX"),                E::VERT_TEX8_INDX },
                    { GetParamNameByPool("VERTEX_TEX9_INDX"),                E::VERT_TEX9_INDX },
                    { GetParamNameByPool("VERTEX_TEX10_INDX"),               E::VERT_TEX10_INDX },
                    { GetParamNameByPool("VERTEX_TEX11_INDX"),               E::VERT_TEX11_INDX },

                    { GetParamNameByPool("VERTEX_BLEND_INDX0"),              E::VERT_BLEND_INDX0 },
                    { GetParamNameByPool("VERTEX_BLEND_INDX1"),              E::VERT_BLEND_INDX1 },

                    { GetParamNameByPool("VERTEX_MATRIX_ROW1_INDX_INSTANCE"),E::VERT_MATRIX_ROW1_INDX_INSTANCE },
                    { GetParamNameByPool("VERTEX_MATRIX_ROW2_INDX_INSTANCE"),E::VERT_MATRIX_ROW2_INDX_INSTANCE },
                    { GetParamNameByPool("VERTEX_MATRIX_ROW3_INDX_INSTANCE"),E::VERT_MATRIX_ROW3_INDX_INSTANCE },
                    { GetParamNameByPool("VERTEX_MATRIX_ROW4_INDX_INSTANCE"),E::VERT_MATRIX_ROW4_INDX_INSTANCE },
                    { GetParamNameByPool("VERTEX_MATRIX_ROW5_INDX_INSTANCE"),E::VERT_MATRIX_ROW5_INDX_INSTANCE },
                    { GetParamNameByPool("VERTEX_POINT_LIGHT_INDX_INSTANCE"),E::VERT_POINT_LIGHT_INDX_INSTANCE },
                };
                return m;
            }();

        auto it = kTable.find(pName);

        if (it != kTable.end())
            return it->second;

        return E::COUNT;
    }

    KVertexDecl& KVertexDecl::Add(
        KAttribUsage::Enum    attribUsage,
        unsigned              baseAttribCount,
        KBaseAttribType::Enum baseAttribType,
        KVertType             eVertType,
        BOOL                  bAsInt
    )
    {
        m_ItemTable[m_nItem] = attribUsage;
        m_Attr[attribUsage].m_offset = m_stride;
        m_Attr[attribUsage].m_bAsInt = bAsInt;
        m_Attr[attribUsage].m_baseAttribCount = baseAttribCount;
        m_Attr[attribUsage].m_baseAttribType = baseAttribType;
        if (eVertType == COMM_VERT)
        {
            m_Attr[attribUsage].m_szLocationName = GetCommVertName(attribUsage);
        }
        else if (eVertType == SKIN_WEIGHT_VERT)
        {
            m_Attr[attribUsage].m_szLocationName = GetSkinVertName(attribUsage);
        }
        else if (eVertType == INSTANCE_VERT)
        {
            m_Attr[attribUsage].m_szLocationName = GetInstanceVertName(attribUsage);
        }
        ASSERT(m_Attr[attribUsage].m_szLocationName);

        m_stride += baseAttribCount * baseAtt[baseAttribType];
        m_nItem++;
        return *this;
    }

    KVertexDecl& KVertexDecl::Add(KAttribUsage::Enum attribUsage, unsigned offset, unsigned baseAttribCount, KBaseAttribType::Enum baseAttribType, KVertType eVertType, BOOL bAsInt /*= false*/)
    {
        m_ItemTable[m_nItem] = attribUsage;
        m_Attr[attribUsage].m_offset = offset;
        m_Attr[attribUsage].m_bAsInt = bAsInt;
        m_Attr[attribUsage].m_baseAttribCount = baseAttribCount;
        m_Attr[attribUsage].m_baseAttribType = baseAttribType;
        if (eVertType == COMM_VERT)
        {
            m_Attr[attribUsage].m_szLocationName = GetCommVertName(attribUsage);
        }
        else if (eVertType == SKIN_WEIGHT_VERT)
        {
            m_Attr[attribUsage].m_szLocationName = GetSkinVertName(attribUsage);
        }
        else if (eVertType == INSTANCE_VERT)
        {
            m_Attr[attribUsage].m_szLocationName = GetInstanceVertName(attribUsage);
        }
        ASSERT(m_Attr[attribUsage].m_szLocationName);

        m_stride += baseAttribCount * baseAtt[baseAttribType];
        m_nItem++;
        return *this;
    }

    void InitVertexDesc()
    {
        P_VERT::decl
            .Add(KAttribUsage::VERT_POS_INDX, 3, KBaseAttribType::Float, COMM_VERT);

        P4_VERT::decl
            .Add(KAttribUsage::VERT_POS_INDX, 4, KBaseAttribType::Float, COMM_VERT);

        POINT_CLOUD_VERT::decl
            .Add(KAttribUsage::VERT_TEX0_INDX, 2, KBaseAttribType::Uint16, COMM_VERT)
            .Add(KAttribUsage::VERT_TEX1_INDX, 2, KBaseAttribType::Uint16, COMM_VERT);

        PT_VERT::decl
            .Add(KAttribUsage::VERT_POS_INDX, 3, KBaseAttribType::Float, COMM_VERT)
            .Add(KAttribUsage::VERT_TEX0_INDX, 2, KBaseAttribType::Float, COMM_VERT);

        TERRAIN_VERT::decl
#if TERRAIN_POS_DEBUG
            .Add(KAttribUsage::VERT_POS_INDX, 3, KBaseAttribType::Float, COMM_VERT)
#endif
            .Add(KAttribUsage::VERT_COLOR_INDX, 4, KBaseAttribType::Uint8, COMM_VERT)
            .Add(KAttribUsage::VERT_TEX0_INDX, 1, KBaseAttribType::Float, COMM_VERT);

        LANDSCAPE_VERT::decl
            .Add(KAttribUsage::VERT_POS_INDX, 2, KBaseAttribType::Float, COMM_VERT);

        LANDSCAPE_INSTANCE0_VERT::decl
            .Add(KAttribUsage::VERT_MATRIX_ROW1_INDX_INSTANCE, 4, KBaseAttribType::Float, INSTANCE_VERT)
            .Add(KAttribUsage::VERT_MATRIX_ROW2_INDX_INSTANCE, 4, KBaseAttribType::Float, INSTANCE_VERT);

        T1_VERT::decl
            .Add(KAttribUsage::VERT_TEX1_INDX, 2, KBaseAttribType::Float, COMM_VERT);

        T2_VERT::decl
            .Add(KAttribUsage::VERT_TEX2_INDX, 2, KBaseAttribType::Float, COMM_VERT);

        T3_VERT::decl
            .Add(KAttribUsage::VERT_TEX3_INDX, 2, KBaseAttribType::Float, COMM_VERT);

        T2_UVW_VERT::decl.Add(KAttribUsage::VERT_TEX2_INDX, 3, KBaseAttribType::Float, COMM_VERT);


        P2T_VERT::decl
            .Add(KAttribUsage::VERT_POS_INDX, 3, KBaseAttribType::Float, COMM_VERT)
            .Add(KAttribUsage::VERT_TEX0_INDX, 2, KBaseAttribType::Float, COMM_VERT)
            .Add(KAttribUsage::VERT_TEX1_INDX, 2, KBaseAttribType::Float, COMM_VERT);

        P2TC_VERT::decl
            .Add(KAttribUsage::VERT_POS_INDX, 3, KBaseAttribType::Float, COMM_VERT)
            .Add(KAttribUsage::VERT_TEX0_INDX, 2, KBaseAttribType::Float, COMM_VERT)
            .Add(KAttribUsage::VERT_TEX1_INDX, 2, KBaseAttribType::Float, COMM_VERT)
            .Add(KAttribUsage::VERT_COLOR_INDX, 4, KBaseAttribType::Uint8, COMM_VERT);

        PT3_VERT::decl
            .Add(KAttribUsage::VERT_POS_INDX, 3, KBaseAttribType::Float, COMM_VERT)
            .Add(KAttribUsage::VERT_TEX0_INDX, 3, KBaseAttribType::Float, COMM_VERT);

        PNT_VERT::decl
            .Add(KAttribUsage::VERT_POS_INDX, 3, KBaseAttribType::Float, COMM_VERT)
            .Add(KAttribUsage::VERT_NORMAL_INDX, 3, KBaseAttribType::Float, COMM_VERT)
            .Add(KAttribUsage::VERT_TEX0_INDX, 2, KBaseAttribType::Float, COMM_VERT);

        PN2T_VERT::decl
            .Add(KAttribUsage::VERT_POS_INDX, 3, KBaseAttribType::Float, COMM_VERT)
            .Add(KAttribUsage::VERT_NORMAL_INDX, 3, KBaseAttribType::Float, COMM_VERT)
            .Add(KAttribUsage::VERT_TEX0_INDX, 2, KBaseAttribType::Float, COMM_VERT)
            .Add(KAttribUsage::VERT_TEX1_INDX, 2, KBaseAttribType::Float, COMM_VERT);

        PC_VERT::decl
            .Add(KAttribUsage::VERT_POS_INDX, 3, KBaseAttribType::Float, COMM_VERT)
            .Add(KAttribUsage::VERT_COLOR_INDX, 4, KBaseAttribType::Uint8, COMM_VERT);

        PNCGT_VERT::decl
            .Add(KAttribUsage::VERT_POS_INDX, 3, KBaseAttribType::Float, COMM_VERT)
            .Add(KAttribUsage::VERT_NORMAL_INDX, 3, KBaseAttribType::Float, COMM_VERT)
            .Add(KAttribUsage::VERT_COLOR_INDX, 4, KBaseAttribType::Uint8, COMM_VERT)
            .Add(KAttribUsage::VERT_TANGENT_INDX, 4, KBaseAttribType::Float, COMM_VERT)
            .Add(KAttribUsage::VERT_TEX0_INDX, 2, KBaseAttribType::Float, COMM_VERT);

        PNCG2T_VERT::decl
            .Add(KAttribUsage::VERT_POS_INDX, 3, KBaseAttribType::Float, COMM_VERT)
            .Add(KAttribUsage::VERT_NORMAL_INDX, 3, KBaseAttribType::Float, COMM_VERT)
            .Add(KAttribUsage::VERT_COLOR_INDX, 4, KBaseAttribType::Uint8, COMM_VERT)
            .Add(KAttribUsage::VERT_TANGENT_INDX, 4, KBaseAttribType::Float, COMM_VERT)
            .Add(KAttribUsage::VERT_TEX0_INDX, 2, KBaseAttribType::Float, COMM_VERT)
            .Add(KAttribUsage::VERT_TEX1_INDX, 2, KBaseAttribType::Float, COMM_VERT);

        PSS_VERT::decl
            .Add(KAttribUsage::VERT_POS_INDX, 3, KBaseAttribType::Float, COMM_VERT)
            .Add(KAttribUsage::VERT_TEX0_INDX, 2, KBaseAttribType::Float, COMM_VERT)
            .Add(KAttribUsage::VERT_COLOR_INDX, 4, KBaseAttribType::Uint8, COMM_VERT)
            .Add(KAttribUsage::VERT_NORMAL_INDX, 3, KBaseAttribType::Float, COMM_VERT);

        PSSMESH_VERT::decl
            .Add(KAttribUsage::VERT_POS_INDX, 3, KBaseAttribType::Float, COMM_VERT)
            .Add(KAttribUsage::VERT_NORMAL_INDX, 3, KBaseAttribType::Float, COMM_VERT)
            .Add(KAttribUsage::VERT_COLOR_INDX, 4, KBaseAttribType::Uint8, COMM_VERT)
            .Add(KAttribUsage::VERT_TEX0_INDX, 2, KBaseAttribType::Float, COMM_VERT)
            .Add(KAttribUsage::VERT_TANGENT_INDX, 4, KBaseAttribType::Float, COMM_VERT);

        MAT44_INSTNCE_VERT::decl
            .Add(KAttribUsage::VERT_MATRIX_ROW1_INDX_INSTANCE, 4, KBaseAttribType::Float, INSTANCE_VERT)
            .Add(KAttribUsage::VERT_MATRIX_ROW2_INDX_INSTANCE, 4, KBaseAttribType::Float, INSTANCE_VERT)
            .Add(KAttribUsage::VERT_MATRIX_ROW3_INDX_INSTANCE, 4, KBaseAttribType::Float, INSTANCE_VERT)
            .Add(KAttribUsage::VERT_MATRIX_ROW4_INDX_INSTANCE, 4, KBaseAttribType::Float, INSTANCE_VERT);

        VEC4_INSTANCE_VERT::decl
            .Add(KAttribUsage::VERT_MATRIX_ROW1_INDX_INSTANCE, 4, KBaseAttribType::Float, INSTANCE_VERT);


        MAT34_INSTNCE_VERT::decl
            .Add(KAttribUsage::VERT_MATRIX_ROW1_INDX_INSTANCE, 4, KBaseAttribType::Float, INSTANCE_VERT)
            .Add(KAttribUsage::VERT_MATRIX_ROW2_INDX_INSTANCE, 4, KBaseAttribType::Float, INSTANCE_VERT)
            .Add(KAttribUsage::VERT_MATRIX_ROW3_INDX_INSTANCE, 4, KBaseAttribType::Float, INSTANCE_VERT);

        BONE_WEIGHT_VERT::decl
            .Add(KAttribUsage::VERT_BLEND_INDX0, 4, KBaseAttribType::Float, SKIN_WEIGHT_VERT)
            .Add(KAttribUsage::VERT_BLEND_INDX1, 4, KBaseAttribType::Float, SKIN_WEIGHT_VERT);

        PCT_VERT::decl
            .Add(KAttribUsage::VERT_POS_INDX, 3, KBaseAttribType::Float, COMM_VERT)
            .Add(KAttribUsage::VERT_COLOR_INDX, 4, KBaseAttribType::Uint8, COMM_VERT)
            .Add(KAttribUsage::VERT_TEX0_INDX, 2, KBaseAttribType::Float, COMM_VERT);

        SPEEDTREE_HD_VERT::decl
            .Add(KAttribUsage::VERT_POS_INDX, 3, KBaseAttribType::Float, COMM_VERT)
            .Add(KAttribUsage::VERT_NORMAL_INDX, 3, KBaseAttribType::Float, COMM_VERT)
            .Add(KAttribUsage::VERT_TANGENT_INDX, 3, KBaseAttribType::Float, COMM_VERT)
            .Add(KAttribUsage::VERT_TEX0_INDX, 4, KBaseAttribType::Float, COMM_VERT)
            .Add(KAttribUsage::VERT_TEX1_INDX, 4, KBaseAttribType::Float, COMM_VERT)
            .Add(KAttribUsage::VERT_TEX2_INDX, 4, KBaseAttribType::Float, COMM_VERT)
            .Add(KAttribUsage::VERT_TEX3_INDX, 4, KBaseAttribType::Float, COMM_VERT)
            .Add(KAttribUsage::VERT_TEX4_INDX, 3, KBaseAttribType::Float, COMM_VERT)
            .Add(KAttribUsage::VERT_TEX5_INDX, 2, KBaseAttribType::Float, COMM_VERT);
        // static_assert(sizeof(SPEEDTREE_HD_VERT) == sizeof(SpeedTreeRawMesh::Vertex));

        SPEEDTREE_HD_BILLBOARD_VERT::decl
            .Add(KAttribUsage::VERT_POS_INDX, 3, KBaseAttribType::Float, COMM_VERT)
            .Add(KAttribUsage::VERT_NORMAL_INDX, 3, KBaseAttribType::Float, COMM_VERT)
            .Add(KAttribUsage::VERT_TANGENT_INDX, 3, KBaseAttribType::Float, COMM_VERT)
            .Add(KAttribUsage::VERT_TEX0_INDX, 2, KBaseAttribType::Float, COMM_VERT)
            .Add(KAttribUsage::VERT_TEX1_INDX, 2, KBaseAttribType::Float, COMM_VERT);

        SPEEDTREE_HD_INSTANCE_VERT::decl
            .Add(KAttribUsage::VERT_MATRIX_ROW1_INDX_INSTANCE, 4, KBaseAttribType::Float, INSTANCE_VERT)
            .Add(KAttribUsage::VERT_MATRIX_ROW2_INDX_INSTANCE, 4, KBaseAttribType::Float, INSTANCE_VERT)
            .Add(KAttribUsage::VERT_MATRIX_ROW3_INDX_INSTANCE, 4, KBaseAttribType::Float, INSTANCE_VERT);
        //.Add(KAttribUsage::VERT_TEX9_INDX, 4, KBaseAttribType::Float);

        AABBBOX_OCCLUSION_CULL_INSTANCE_VERT::decl
            .Add(KAttribUsage::VERT_MATRIX_ROW1_INDX_INSTANCE, 4, KBaseAttribType::Float, INSTANCE_VERT)
            .Add(KAttribUsage::VERT_MATRIX_ROW2_INDX_INSTANCE, 4, KBaseAttribType::Float, INSTANCE_VERT);

        PSS_INSTANCE_VERT::decl
            .Add(KAttribUsage::VERT_MATRIX_ROW1_INDX_INSTANCE, 4, KBaseAttribType::Float, INSTANCE_VERT)
            .Add(KAttribUsage::VERT_MATRIX_ROW2_INDX_INSTANCE, 4, KBaseAttribType::Float, INSTANCE_VERT)
            .Add(KAttribUsage::VERT_MATRIX_ROW3_INDX_INSTANCE, 4, KBaseAttribType::Float, INSTANCE_VERT)
            .Add(KAttribUsage::VERT_MATRIX_ROW4_INDX_INSTANCE, 4, KBaseAttribType::Float, INSTANCE_VERT)
            .Add(KAttribUsage::VERT_MATRIX_ROW5_INDX_INSTANCE, 4, KBaseAttribType::Float, INSTANCE_VERT);

        SFXBILLBOARD_VERT::decl
            .Add(KAttribUsage::VERT_POS_INDX, 3, KBaseAttribType::Float, COMM_VERT)
            .Add(KAttribUsage::VERT_COLOR_INDX, 4, KBaseAttribType::Uint8, COMM_VERT)
            .Add(KAttribUsage::VERT_TEX0_INDX, 2, KBaseAttribType::Float, COMM_VERT)
            .Add(KAttribUsage::VERT_TEX1_INDX, 2, KBaseAttribType::Float, COMM_VERT);

        SFXPARTICLE_VERT::decl
            .Add(KAttribUsage::VERT_POS_INDX, 3, KBaseAttribType::Float, COMM_VERT)
            .Add(KAttribUsage::VERT_COLOR_INDX, 4, KBaseAttribType::Uint8, COMM_VERT)
            .Add(KAttribUsage::VERT_TEX0_INDX, 2, KBaseAttribType::Float, COMM_VERT);

        WATER_VERT::decl
            .Add(KAttribUsage::VERT_POS_INDX, 4, KBaseAttribType::Float, COMM_VERT);

        WATER_INSTANCE_VERT::decl
            .Add(KAttribUsage::VERT_MATRIX_ROW1_INDX_INSTANCE, 4, KBaseAttribType::Float, INSTANCE_VERT)
            .Add(KAttribUsage::VERT_MATRIX_ROW2_INDX_INSTANCE, 4, KBaseAttribType::Float, INSTANCE_VERT);
    }
    ////////////////////////////////////////////////////////////////////


    KVertexDecl P_VERT::decl(P_VERT_DECL);
    KVertexDecl P4_VERT::decl(P4_VERT_DECL);
    KVertexDecl POINT_CLOUD_VERT::decl(POINT_CLOUD_DECL);
    KVertexDecl PT_VERT::decl(PT_VERT_DECL);
    KVertexDecl TERRAIN_VERT::decl(TERRAIN_VERT_DECL);
    KVertexDecl LANDSCAPE_VERT::decl(LANDSCAPE_VERT_DECL);
    KVertexDecl LANDSCAPE_INSTANCE0_VERT::decl(LANDSCAPE_INSTANCE0_VERT_DECL);
    KVertexDecl T1_VERT::decl(T1_VERT_DECL);
    KVertexDecl T2_VERT::decl(T2_VERT_DECL);
    KVertexDecl T3_VERT::decl(T3_VERT_DECL);
    KVertexDecl T2_UVW_VERT::decl(T2_UVW_VERT_DECL);

    KVertexDecl P2T_VERT::decl(P2T_VERT_DECL);
    KVertexDecl P2TC_VERT::decl(P2TC_VERT_DECL);
    KVertexDecl PT3_VERT::decl(PT3_VERT_DECL);

    KVertexDecl PNT_VERT::decl(PNT_VERT_DECL);
    KVertexDecl PN2T_VERT::decl(PN2T_VERT_DECL);
    KVertexDecl PC_VERT::decl(PC_VERT_DECL);
    KVertexDecl PNCGT_VERT::decl(PNCGT_VERT_DECL);
    KVertexDecl PSS_VERT::decl(PSS_VERT_DECL);
    KVertexDecl PSSMESH_VERT::decl(PSSMESH_VERT_DECL);
    KVertexDecl PNCG2T_VERT::decl(PNCG2T_VERT_DECL);
    KVertexDecl MAT44_INSTNCE_VERT::decl(MAT44_INSTANCE_VERT_DECL);
    KVertexDecl MAT34_INSTNCE_VERT::decl(MAT34_INSTANCE_VERT_DECL);
    KVertexDecl BONE_WEIGHT_VERT::decl(BONE_WEIGHT_DECL);
    KVertexDecl PCT_VERT::decl(PCT_VERT_DECL);
    KVertexDecl SPEEDTREE_HD_VERT::decl(SPEEDTREE_HD_VERT_DECL);
    KVertexDecl SPEEDTREE_HD_BILLBOARD_VERT::decl(SPEEDTREE_HD_BILLBOARD_VERT_DECL);
    KVertexDecl SPEEDTREE_HD_INSTANCE_VERT::decl(SPEEDTREE_HD_INSANCE_VERT_DECL);
    KVertexDecl VEC4_INSTANCE_VERT::decl(VEC4_INSTANCE_VERT_DECL);
    KVertexDecl AABBBOX_OCCLUSION_CULL_INSTANCE_VERT::decl(AABBBOX_OCCLUSION_CULL_INSTANCE_VERT_DECL);
    KVertexDecl PSS_INSTANCE_VERT::decl(PSS_INSTANCE_VERT_DECL);
    KVertexDecl SFXBILLBOARD_VERT::decl(SFX_BILLBOARD_VERT_DECL);
    KVertexDecl SFXPARTICLE_VERT::decl(SFX_PARTICLE_DECL);
    KVertexDecl WATER_VERT::decl(WATER_DECL);
    KVertexDecl WATER_INSTANCE_VERT::decl(WATER_INSTANCE_DECL);

    /////////////////////////////////////////////////////////////////////////
    inline uint32_t _GetHashCodeForMem32Bit(const char* buffer, size_t size)
    {
        uint32_t    uHash = 0;
        const char* p = buffer;
        for (size_t i = 0; i < size; ++i)
        {
            uHash = (uHash << 7) + (uHash << 1) + uHash + *p++;
        }
        return uHash;
    }

    KRenderState::data::_dt::_dt()
    {
        memset(this, 0, sizeof(KRenderState::data::_dt));
        depthBiasConstantFactor = 0.f;
        depthBiasClamp = 0.f;
        depthBiasSlopeFactor = 0.f;
        minDepthBounds = 0.f;
        maxDepthBounds = 1.f;
        lineWidth = 1.f;


        viewPort.x = 0.f;
        viewPort.y = 0.f;
        viewPort.width = 1.f;
        viewPort.height = 1.f;
        viewPort.minDepth = 0.f;
        viewPort.maxDepth = 1.f;

        scissor.offsetX = 0;
        scissor.offsetY = 0;
        scissor.extendWidth = 1024;
        scissor.extendHeight = 1024;

        msaa = 0;
        msaa_line = 0;
        sampleShadingEnable = 0;
        sampleCountFlag = SAMPLE_COUNT_1_BIT;
        sampleAlphaToCoverageEnable = 0;
        sampleAlphaToOneEnable = 0;

        polygonMode = POLYGON_MODE_FILL;
        frontFaceMode = FRONT_FACE_COUNTER_CLOCKWISE;
        cullMode = CULL_MODE_BACK;
        drawMode = PT_TRIANGLE_LIST;
        depthBiasEnable = 0;
        depthClampEnable = 0;
        rasterizerDiscardEnable = 0;
        blendAttachCount = 1;
        mrtMask = 0;

        alphaRef = 128;
        pointSize = 16;

        depthTestEnable = 1;
        depthWriteEnable = 1;
        if (DrvOption::bReversePerspectiveDepthZ)
        {
            depthCompareOp = DEPTH_TEST_GEQUAL;
        }
        else
        {
            depthCompareOp = DEPTH_TEST_LEQUAL;
        }
        depthBoundsTestEnable = 0;

        stencilTestEnable = 0;


        stencilFront.compareMask = 0xffffff;
        stencilFront.writeMask = 0xffffff;
        stencilFront.reference = 0;
        stencilFront.stencilCompareOp = STENCIL_TEST_LESS;
        stencilFront.sencilFailOp = STENCIL_OP_KEEP;
        stencilFront.stencilPassOp = STENCIL_OP_KEEP;
        stencilFront.stencilDepthFailOp = STENCIL_OP_KEEP;


        stencilBack.compareMask = 0xffffff;
        stencilBack.writeMask = 0xffffff;
        stencilBack.reference = 0;
        stencilBack.stencilCompareOp = STENCIL_TEST_LESS;
        stencilBack.sencilFailOp = STENCIL_OP_KEEP;
        stencilBack.stencilPassOp = STENCIL_OP_KEEP;
        stencilBack.stencilDepthFailOp = STENCIL_OP_KEEP;

        for (uint32_t i = 0; i < KMAX_BLEND_ATTACHMENT; ++i)
        {
            blendAttachment[i].blendEnable = 0;
            blendAttachment[i].writeR = 1;
            blendAttachment[i].writeG = 1;
            blendAttachment[i].writeB = 1;
            blendAttachment[i].writeA = 1;

            blendAttachment[i].srcColorBlendFactor = BLEND_ONE;
            blendAttachment[i].dstColorBlendFactor = BLEND_ZERO;
            blendAttachment[i].srcAlphaBlendFactor = BLEND_ONE;
            blendAttachment[i].dstAlphaBlendFactor = BLEND_ZERO;

            blendAttachment[i].colorBlendOp = BLEND_EQUATION_ADD;
            blendAttachment[i].alphaBlendOp = BLEND_EQUATION_ADD;
        }

        defaultViewPortEnable = 1;
        defaultScissorEnable = 1;
    }

    KRenderState::KRenderState()
    {
        d.ZeroHash();
    }

    // DeclareProperty(KRenderState, float, depthBiasConstantFactor);
    KRenderState::_depthBiasConstantFactor& KRenderState::_depthBiasConstantFactor::operator=(const float& other)
    {
        KRenderState::_depthBiasConstantFactor* p1 = &((KRenderState*)0)->depthBiasConstantFactor;
        size_t                                  offset = (size_t)((char*)(p1));
        data* p = (data*)(this - offset);
        p->dt.depthBiasConstantFactor = other;
        p->ZeroHash();
        return *this;
    }

    KRenderState::_depthBiasConstantFactor::operator float()
    {
        KRenderState::_depthBiasConstantFactor* p1 = &((KRenderState*)0)->depthBiasConstantFactor;
        size_t                                  offset = (size_t)((char*)(p1));
        data* p = (data*)(this - offset);
        return p->dt.depthBiasConstantFactor;
    }

    // DeclareProperty(KRenderState, float, depthBiasClamp);
    KRenderState::_depthBiasClamp& KRenderState::_depthBiasClamp::operator=(const float& other)
    {
        KRenderState::_depthBiasClamp* p1 = &((KRenderState*)0)->depthBiasClamp;
        size_t                         offset = (size_t)((char*)(p1));
        data* p = (data*)(this - offset);
        p->dt.depthBiasClamp = other;
        p->ZeroHash();
        return *this;
    }

    KRenderState::_depthBiasClamp::operator float()
    {
        KRenderState::_depthBiasClamp* p1 = &((KRenderState*)0)->depthBiasClamp;
        size_t                         offset = (size_t)((char*)(p1));
        data* p = (data*)(this - offset);
        return p->dt.depthBiasClamp;
    }

    // DeclareProperty(KRenderState, float, depthBiasSlopeFactor);
    KRenderState::_depthBiasSlopeFactor& KRenderState::_depthBiasSlopeFactor::operator=(const float& other)
    {
        KRenderState::_depthBiasSlopeFactor* p1 = &((KRenderState*)0)->depthBiasSlopeFactor;
        size_t                               offset = (size_t)((char*)(p1));
        data* p = (data*)(this - offset);
        p->dt.depthBiasSlopeFactor = other;
        p->ZeroHash();
        return *this;
    }
    KRenderState::_depthBiasSlopeFactor::operator float()
    {
        KRenderState::_depthBiasSlopeFactor* p1 = &((KRenderState*)0)->depthBiasSlopeFactor;
        size_t                               offset = (size_t)((char*)(p1));
        data* p = (data*)(this - offset);
        return p->dt.depthBiasSlopeFactor;
    }

    // DeclareProperty(KRenderState, float, minDepthBounds);
    KRenderState::_minDepthBounds& KRenderState::_minDepthBounds::operator=(const float& other)
    {
        KRenderState::_minDepthBounds* p1 = &((KRenderState*)0)->minDepthBounds;
        size_t                         offset = (size_t)((char*)(p1));
        data* p = (data*)(this - offset);
        p->dt.minDepthBounds = other;
        p->ZeroHash();
        return *this;
    }
    KRenderState::_minDepthBounds::operator float()
    {
        KRenderState::_minDepthBounds* p1 = &((KRenderState*)0)->minDepthBounds;
        size_t                         offset = (size_t)((char*)(p1));
        data* p = (data*)(this - offset);
        return p->dt.minDepthBounds;
    }

    // DeclareProperty(KRenderState, float, maxDepthBounds);
    KRenderState::_maxDepthBounds& KRenderState::_maxDepthBounds::operator=(const float& other)
    {
        KRenderState::_maxDepthBounds* p1 = &((KRenderState*)0)->maxDepthBounds;
        size_t                         offset = (size_t)((char*)(p1));
        data* p = (data*)(this - offset);
        p->dt.maxDepthBounds = other;
        p->ZeroHash();
        return *this;
    }
    KRenderState::_maxDepthBounds::operator float()
    {
        KRenderState::_maxDepthBounds* p1 = &((KRenderState*)0)->maxDepthBounds;
        size_t                         offset = (size_t)((char*)(p1));
        data* p = (data*)(this - offset);
        return p->dt.maxDepthBounds;
    }

    // DeclareProperty(KRenderState, float, lineWidth);
    KRenderState::_lineWidth& KRenderState::_lineWidth::operator=(const float& other)
    {
        KRenderState::_lineWidth* p1 = &((KRenderState*)0)->lineWidth;
        size_t                    offset = (size_t)((char*)(p1));
        data* p = (data*)(this - offset);
        p->dt.lineWidth = other;
        p->ZeroHash();
        return *this;
    }
    KRenderState::_lineWidth::operator float()
    {
        KRenderState::_lineWidth* p1 = &((KRenderState*)0)->lineWidth;
        size_t                    offset = (size_t)((char*)(p1));
        data* p = (data*)(this - offset);
        return p->dt.lineWidth;
    }

    // DeclareSecondProperty(KRenderState, float, viewPort, x);
    KRenderState::_viewPort::_x& KRenderState::_viewPort::_x::operator=(const float& other)
    {
        KRenderState::_viewPort::_x* p1 = &((KRenderState*)0)->viewPort.x;
        size_t                       offset = (size_t)((char*)(p1));
        data* p = (data*)(this - offset);
        p->dt.viewPort.x = other;
        p->ZeroHash();
        return *this;
    }

    KRenderState::_viewPort::_x::operator float()
    {
        KRenderState::_viewPort::_x* p1 = &((KRenderState*)0)->viewPort.x;
        size_t                       offset = (size_t)((char*)(p1));
        data* p = (data*)(this - offset);
        return p->dt.viewPort.x;
    }

    // DeclareSecondProperty(KRenderState, float, viewPort, y);
    KRenderState::_viewPort::_y& KRenderState::_viewPort::_y::operator=(const float& other)
    {
        KRenderState::_viewPort::_y* p1 = &((KRenderState*)0)->viewPort.y;
        size_t                       offset = (size_t)((char*)(p1));
        data* p = (data*)(this - offset);
        p->dt.viewPort.y = other;
        p->ZeroHash();
        return *this;
    }

    KRenderState::_viewPort::_y::operator float()
    {
        KRenderState::_viewPort::_y* p1 = &((KRenderState*)0)->viewPort.y;
        size_t                       offset = (size_t)((char*)(p1));
        data* p = (data*)(this - offset);
        return p->dt.viewPort.y;
    }

    // DeclareSecondProperty(KRenderState, float, viewPort, width);
    KRenderState::_viewPort::_width& KRenderState::_viewPort::_width::operator=(const float& other)
    {
        KRenderState::_viewPort::_width* p1 = &((KRenderState*)0)->viewPort.width;
        size_t                           offset = (size_t)((char*)(p1));
        data* p = (data*)(this - offset);
        p->dt.viewPort.width = other;
        p->ZeroHash();
        return *this;
    }

    KRenderState::_viewPort::_width::operator float()
    {
        KRenderState::_viewPort::_width* p1 = &((KRenderState*)0)->viewPort.width;
        size_t                           offset = (size_t)((char*)(p1));
        data* p = (data*)(this - offset);
        return p->dt.viewPort.width;
    }

    // DeclareSecondProperty(KRenderState, float, viewPort, height);
    KRenderState::_viewPort::_height& KRenderState::_viewPort::_height::operator=(const float& other)
    {
        KRenderState::_viewPort::_height* p1 = &((KRenderState*)0)->viewPort.height;
        size_t                            offset = (size_t)((char*)(p1));
        data* p = (data*)(this - offset);
        p->dt.viewPort.height = other;
        p->ZeroHash();
        return *this;
    }

    KRenderState::_viewPort::_height::operator float()
    {
        KRenderState::_viewPort::_height* p1 = &((KRenderState*)0)->viewPort.height;
        size_t                            offset = (size_t)((char*)(p1));
        data* p = (data*)(this - offset);
        return p->dt.viewPort.height;
    }

    // DeclareSecondProperty(KRenderState, float, viewPort, minDepth);
    KRenderState::_viewPort::_minDepth& KRenderState::_viewPort::_minDepth::operator=(const float& other)
    {
        KRenderState::_viewPort::_minDepth* p1 = &((KRenderState*)0)->viewPort.minDepth;
        size_t                              offset = (size_t)((char*)(p1));
        data* p = (data*)(this - offset);
        p->dt.viewPort.minDepth = other;
        p->ZeroHash();
        return *this;
    }

    KRenderState::_viewPort::_minDepth::operator float()
    {
        KRenderState::_viewPort::_minDepth* p1 = &((KRenderState*)0)->viewPort.minDepth;
        size_t                              offset = (size_t)((char*)(p1));
        data* p = (data*)(this - offset);
        return p->dt.viewPort.minDepth;
    }

    // DeclareSecondProperty(KRenderState, float, viewPort, maxDepth);
    KRenderState::_viewPort::_maxDepth& KRenderState::_viewPort::_maxDepth::operator=(const float& other)
    {
        KRenderState::_viewPort::_maxDepth* p1 = &((KRenderState*)0)->viewPort.maxDepth;
        size_t                              offset = (size_t)((char*)(p1));
        data* p = (data*)(this - offset);
        p->dt.viewPort.maxDepth = other;
        p->ZeroHash();
        return *this;
    }

    KRenderState::_viewPort::_maxDepth::operator float()
    {
        KRenderState::_viewPort::_maxDepth* p1 = &((KRenderState*)0)->viewPort.maxDepth;
        size_t                              offset = (size_t)((char*)(p1));
        data* p = (data*)(this - offset);
        return p->dt.viewPort.maxDepth;
    }

    // DeclareSecondProperty(KRenderState, int32_t, scissor, offsetX);
    KRenderState::_scissor::_offsetX& KRenderState::_scissor::_offsetX::operator=(const int32_t& other)
    {
        KRenderState::_scissor::_offsetX* p1 = &((KRenderState*)0)->scissor.offsetX;
        size_t                            offset = (size_t)((char*)(p1));
        data* p = (data*)(this - offset);
        p->dt.scissor.offsetX = other;
        p->ZeroHash();
        return *this;
    }

    KRenderState::_scissor::_offsetX::operator int32_t()
    {
        KRenderState::_scissor::_offsetX* p1 = &((KRenderState*)0)->scissor.offsetX;
        size_t                            offset = (size_t)((char*)(p1));
        data* p = (data*)(this - offset);
        return p->dt.scissor.offsetX;
    }

    // DeclareSecondProperty(KRenderState, int32_t, scissor, offsetY);
    KRenderState::_scissor::_offsetY& KRenderState::_scissor::_offsetY::operator=(const int32_t& other)
    {
        KRenderState::_scissor::_offsetY* p1 = &((KRenderState*)0)->scissor.offsetY;
        size_t                            offset = (size_t)((char*)(p1));
        data* p = (data*)(this - offset);
        p->dt.scissor.offsetY = other;
        p->ZeroHash();
        return *this;
    }

    KRenderState::_scissor::_offsetY::operator int32_t()
    {
        KRenderState::_scissor::_offsetY* p1 = &((KRenderState*)0)->scissor.offsetY;
        size_t                            offset = (size_t)((char*)(p1));
        data* p = (data*)(this - offset);
        return p->dt.scissor.offsetY;
    }


    // DeclareSecondProperty(KRenderState, uint32_t, scissor, extendWidth);
    KRenderState::_scissor::_extendWidth& KRenderState::_scissor::_extendWidth::operator=(const uint32_t& other)
    {
        KRenderState::_scissor::_extendWidth* p1 = &((KRenderState*)0)->scissor.extendWidth;
        size_t                                offset = (size_t)((char*)(p1));
        data* p = (data*)(this - offset);
        p->dt.scissor.extendWidth = other;
        p->ZeroHash();
        return *this;
    }

    KRenderState::_scissor::_extendWidth::operator uint32_t()
    {
        KRenderState::_scissor::_extendWidth* p1 = &((KRenderState*)0)->scissor.extendWidth;
        size_t                                offset = (size_t)((char*)(p1));
        data* p = (data*)(this - offset);
        return p->dt.scissor.extendWidth;
    }

    // DeclareSecondProperty(KRenderState, uint32_t, scissor, extendHeight);
    KRenderState::_scissor::_extendHeight& KRenderState::_scissor::_extendHeight::operator=(const uint32_t& other)
    {
        KRenderState::_scissor::_extendHeight* p1 = &((KRenderState*)0)->scissor.extendHeight;
        size_t                                 offset = (size_t)((char*)(p1));
        data* p = (data*)(this - offset);
        p->dt.scissor.extendHeight = other;
        p->ZeroHash();
        return *this;
    }

    KRenderState::_scissor::_extendHeight::operator uint32_t()
    {
        KRenderState::_scissor::_extendHeight* p1 = &((KRenderState*)0)->scissor.extendHeight;
        size_t                                 offset = (size_t)((char*)(p1));
        data* p = (data*)(this - offset);
        return p->dt.scissor.extendHeight;
    }

    // DeclareProperty(KRenderState, uint8_t, msaa);
    KRenderState::_msaa& KRenderState::_msaa::operator=(const uint8_t& other)
    {
        KRenderState::_msaa* p1 = &((KRenderState*)0)->msaa;
        size_t               offset = (size_t)((char*)(p1));
        data* p = (data*)(this - offset);
        p->dt.msaa = other;
        p->ZeroHash();
        return *this;
    }

    KRenderState::_msaa::operator uint8_t()
    {
        KRenderState::_msaa* p1 = &((KRenderState*)0)->msaa;
        size_t               offset = (size_t)((char*)(p1));
        data* p = (data*)(this - offset);
        return p->dt.msaa;
    }

    // DeclareProperty(KRenderState, uint8_t, msaa_line);
    KRenderState::_msaa_line& KRenderState::_msaa_line::operator=(const uint8_t& other)
    {
        KRenderState::_msaa_line* p1 = &((KRenderState*)0)->msaa_line;
        size_t                    offset = (size_t)((char*)(p1));
        data* p = (data*)(this - offset);
        p->dt.msaa_line = other;
        p->ZeroHash();
        return *this;
    }
    KRenderState::_msaa_line::operator uint8_t()
    {
        KRenderState::_msaa_line* p1 = &((KRenderState*)0)->msaa_line;
        size_t                    offset = (size_t)((char*)(p1));
        data* p = (data*)(this - offset);
        return p->dt.msaa_line;
    }

    // DeclareProperty(KRenderState, uint8_t, sampleShadingEnable);
    KRenderState::_sampleShadingEnable& KRenderState::_sampleShadingEnable::operator=(const uint8_t& other)
    {
        KRenderState::_sampleShadingEnable* p1 = &((KRenderState*)0)->sampleShadingEnable;
        size_t                              offset = (size_t)((char*)(p1));
        data* p = (data*)(this - offset);
        p->dt.sampleShadingEnable = other;
        p->ZeroHash();
        return *this;
    }
    KRenderState::_sampleShadingEnable::operator uint8_t()
    {
        KRenderState::_sampleShadingEnable* p1 = &((KRenderState*)0)->sampleShadingEnable;
        size_t                              offset = (size_t)((char*)(p1));
        data* p = (data*)(this - offset);
        return p->dt.sampleShadingEnable;
    }


    // DeclareProperty(KRenderState, enumSampleCountFlag, sampleCountFlag);
    KRenderState::_sampleCountFlag& KRenderState::_sampleCountFlag::operator=(const enumSampleCountFlag& other)
    {
        KRenderState::_sampleCountFlag* p1 = &((KRenderState*)0)->sampleCountFlag;
        size_t                          offset = (size_t)((char*)(p1));
        data* p = (data*)(this - offset);
        p->dt.sampleCountFlag = other;
        p->ZeroHash();
        return *this;
    }
    KRenderState::_sampleCountFlag::operator enumSampleCountFlag()
    {
        KRenderState::_sampleCountFlag* p1 = &((KRenderState*)0)->sampleCountFlag;
        size_t                          offset = (size_t)((char*)(p1));
        data* p = (data*)(this - offset);
        return p->dt.sampleCountFlag;
    }


    // DeclareProperty(KRenderState, uint8_t, sampleAlphaToCoverageEnable);
    KRenderState::_sampleAlphaToCoverageEnable& KRenderState::_sampleAlphaToCoverageEnable::operator=(const uint8_t& other)
    {
        KRenderState::_sampleAlphaToCoverageEnable* p1 = &((KRenderState*)0)->sampleAlphaToCoverageEnable;
        size_t                                      offset = (size_t)((char*)(p1));
        data* p = (data*)(this - offset);
        p->dt.sampleAlphaToCoverageEnable = other;
        p->ZeroHash();
        return *this;
    }

    KRenderState::_sampleAlphaToCoverageEnable::operator uint8_t()
    {
        KRenderState::_sampleAlphaToCoverageEnable* p1 = &((KRenderState*)0)->sampleAlphaToCoverageEnable;
        size_t                                      offset = (size_t)((char*)(p1));
        data* p = (data*)(this - offset);
        return p->dt.sampleAlphaToCoverageEnable;
    }


    // DeclareProperty(KRenderState, uint8_t, sampleAlphaToOneEnable);
    KRenderState::_sampleAlphaToOneEnable& KRenderState::_sampleAlphaToOneEnable::operator=(const uint8_t& other)
    {
        KRenderState::_sampleAlphaToOneEnable* p1 = &((KRenderState*)0)->sampleAlphaToOneEnable;
        size_t                                 offset = (size_t)((char*)(p1));
        data* p = (data*)(this - offset);
        p->dt.sampleAlphaToOneEnable = other;
        p->ZeroHash();
        return *this;
    }
    KRenderState::_sampleAlphaToOneEnable::operator uint8_t()
    {
        KRenderState::_sampleAlphaToOneEnable* p1 = &((KRenderState*)0)->sampleAlphaToOneEnable;
        size_t                                 offset = (size_t)((char*)(p1));
        data* p = (data*)(this - offset);
        return p->dt.sampleAlphaToOneEnable;
    }


    // DeclareProperty(KRenderState, uint32_t, sampleMask);
    KRenderState::_sampleMask& KRenderState::_sampleMask::operator=(const uint32_t& other)
    {
        KRenderState::_sampleMask* p1 = &((KRenderState*)0)->sampleMask;
        size_t                     offset = (size_t)((char*)(p1));
        data* p = (data*)(this - offset);
        p->dt.sampleMask = other;
        p->ZeroHash();
        return *this;
    }

    KRenderState::_sampleMask::operator uint32_t()
    {
        KRenderState::_sampleMask* p1 = &((KRenderState*)0)->sampleMask;
        size_t                     offset = (size_t)((char*)(p1));
        data* p = (data*)(this - offset);
        return p->dt.sampleMask;
    }


    // DeclareProperty(KRenderState, float, minSampleShading);
    KRenderState::_minSampleShading& KRenderState::_minSampleShading::operator=(const float& other)
    {
        KRenderState::_minSampleShading* p1 = &((KRenderState*)0)->minSampleShading;
        size_t                           offset = (size_t)((char*)(p1));
        data* p = (data*)(this - offset);
        p->dt.minSampleShading = other;
        p->ZeroHash();
        return *this;
    }
    KRenderState::_minSampleShading::operator float()
    {
        KRenderState::_minSampleShading* p1 = &((KRenderState*)0)->minSampleShading;
        size_t                           offset = (size_t)((char*)(p1));
        data* p = (data*)(this - offset);
        return p->dt.minSampleShading;
    }


    // DeclareProperty(KRenderState, enumPolygonMode, polygonMode);
    KRenderState::_polygonMode& KRenderState::_polygonMode::operator=(const enumPolygonMode& other)
    {
        KRenderState::_polygonMode* p1 = &((KRenderState*)0)->polygonMode;
        size_t                      offset = (size_t)((char*)(p1));
        data* p = (data*)(this - offset);
        p->dt.polygonMode = other;
        p->ZeroHash();
        return *this;
    }
    KRenderState::_polygonMode::operator enumPolygonMode()
    {
        KRenderState::_polygonMode* p1 = &((KRenderState*)0)->polygonMode;
        size_t                      offset = (size_t)((char*)(p1));
        data* p = (data*)(this - offset);
        return p->dt.polygonMode;
    }


    // DeclareProperty(KRenderState, enumFrontFaceMode, frontFaceMode);
    KRenderState::_frontFaceMode& KRenderState::_frontFaceMode::operator=(const enumFrontFaceMode& other)
    {
        KRenderState::_frontFaceMode* p1 = &((KRenderState*)0)->frontFaceMode;
        size_t                        offset = (size_t)((char*)(p1));
        data* p = (data*)(this - offset);
        p->dt.frontFaceMode = other;
        p->ZeroHash();
        return *this;
    }
    KRenderState::_frontFaceMode::operator enumFrontFaceMode()
    {
        KRenderState::_frontFaceMode* p1 = &((KRenderState*)0)->frontFaceMode;
        size_t                        offset = (size_t)((char*)(p1));
        data* p = (data*)(this - offset);
        return p->dt.frontFaceMode;
    }


    // DeclareProperty(KRenderState, enumCullMode, cullMode);
    KRenderState::_cullMode& KRenderState::_cullMode::operator=(const enumCullMode& other)
    {
        KRenderState::_cullMode* p1 = &((KRenderState*)0)->cullMode;
        size_t                   offset = (size_t)((char*)(p1));
        data* p = (data*)(this - offset);
        p->dt.cullMode = other;
        p->ZeroHash();
        return *this;
    }
    KRenderState::_cullMode::operator enumCullMode()
    {
        KRenderState::_cullMode* p1 = &((KRenderState*)0)->cullMode;
        size_t                   offset = (size_t)((char*)(p1));
        data* p = (data*)(this - offset);
        return p->dt.cullMode;
    }

    // DeclareProperty(KRenderState, enumDrawMode, drawMode);
    KRenderState::_drawMode& KRenderState::_drawMode::operator=(const enumDrawMode& other)
    {
        KRenderState::_drawMode* p1 = &((KRenderState*)0)->drawMode;
        size_t                   offset = (size_t)((char*)(p1));
        data* p = (data*)(this - offset);
        p->dt.drawMode = other;
        p->ZeroHash();
        return *this;
    }
    KRenderState::_drawMode::operator enumDrawMode()
    {
        KRenderState::_drawMode* p1 = &((KRenderState*)0)->drawMode;
        size_t                   offset = (size_t)((char*)(p1));
        data* p = (data*)(this - offset);
        return p->dt.drawMode;
    }

    // DeclareProperty(KRenderState, uint8_t, depthBiasEnable);
    KRenderState::_depthBiasEnable& KRenderState::_depthBiasEnable::operator=(const uint8_t& other)
    {
        KRenderState::_depthBiasEnable* p1 = &((KRenderState*)0)->depthBiasEnable;
        size_t                          offset = (size_t)((char*)(p1));
        data* p = (data*)(this - offset);
        p->dt.depthBiasEnable = other;
        p->ZeroHash();
        return *this;
    }
    KRenderState::_depthBiasEnable::operator uint8_t()
    {
        KRenderState::_depthBiasEnable* p1 = &((KRenderState*)0)->depthBiasEnable;
        size_t                          offset = (size_t)((char*)(p1));
        data* p = (data*)(this - offset);
        return p->dt.depthBiasEnable;
    }

    // DeclareProperty(KRenderState, uint8_t, depthClampEnable);
    KRenderState::_depthClampEnable& KRenderState::_depthClampEnable::operator=(const uint8_t& other)
    {
        KRenderState::_depthClampEnable* p1 = &((KRenderState*)0)->depthClampEnable;
        size_t                           offset = (size_t)((char*)(p1));
        data* p = (data*)(this - offset);
        p->dt.depthClampEnable = other;
        p->ZeroHash();
        return *this;
    }
    KRenderState::_depthClampEnable::operator uint8_t()
    {
        KRenderState::_depthClampEnable* p1 = &((KRenderState*)0)->depthClampEnable;
        size_t                           offset = (size_t)((char*)(p1));
        data* p = (data*)(this - offset);
        return p->dt.depthClampEnable;
    }

    // DeclareProperty(KRenderState, uint8_t, rasterizerDiscardEnable);
    KRenderState::_rasterizerDiscardEnable& KRenderState::_rasterizerDiscardEnable::operator=(const uint8_t& other)
    {
        KRenderState::_rasterizerDiscardEnable* p1 = &((KRenderState*)0)->rasterizerDiscardEnable;
        size_t                                  offset = (size_t)((char*)(p1));
        data* p = (data*)(this - offset);
        p->dt.rasterizerDiscardEnable = other;
        p->ZeroHash();
        return *this;
    }
    KRenderState::_rasterizerDiscardEnable::operator uint8_t()
    {
        KRenderState::_rasterizerDiscardEnable* p1 = &((KRenderState*)0)->rasterizerDiscardEnable;
        size_t                                  offset = (size_t)((char*)(p1));
        data* p = (data*)(this - offset);
        return p->dt.rasterizerDiscardEnable;
    }


    // DeclareProperty(KRenderState, uint8_t, blendAttachCount);
    KRenderState::_blendAttachCount& KRenderState::_blendAttachCount::operator=(const uint8_t& other)
    {
        KRenderState::_blendAttachCount* p1 = &((KRenderState*)0)->blendAttachCount;
        size_t                           offset = (size_t)((char*)(p1));
        data* p = (data*)(this - offset);
        p->dt.blendAttachCount = other;
        p->ZeroHash();
        return *this;
    }
    KRenderState::_blendAttachCount::operator uint8_t()
    {
        KRenderState::_blendAttachCount* p1 = &((KRenderState*)0)->blendAttachCount;
        size_t                           offset = (size_t)((char*)(p1));
        data* p = (data*)(this - offset);
        return p->dt.blendAttachCount;
    }

    KRenderState::_mrtMask& KRenderState::_mrtMask::operator=(const uint8_t& other)
    {
        KRenderState::_mrtMask* p1 = &((KRenderState*)0)->mrtMask;
        size_t                  offset = (size_t)((char*)(p1));
        data* p = (data*)(this - offset);
        p->dt.mrtMask = other;
        p->ZeroHash();
        return *this;
    }
    KRenderState::_mrtMask::operator uint8_t()
    {
        KRenderState::_mrtMask* p1 = &((KRenderState*)0)->mrtMask;
        size_t                  offset = (size_t)((char*)(p1));
        data* p = (data*)(this - offset);
        return p->dt.mrtMask;
    }


    // DeclareProperty(KRenderState, uint8_t, alphaRef);
    KRenderState::_alphaRef& KRenderState::_alphaRef::operator=(const uint8_t& other)
    {
        KRenderState::_alphaRef* p1 = &((KRenderState*)0)->alphaRef;
        size_t                   offset = (size_t)((char*)(p1));
        data* p = (data*)(this - offset);
        p->dt.alphaRef = other;
        p->ZeroHash();
        return *this;
    }
    KRenderState::_alphaRef::operator uint8_t()
    {
        KRenderState::_alphaRef* p1 = &((KRenderState*)0)->alphaRef;
        size_t                   offset = (size_t)((char*)(p1));
        data* p = (data*)(this - offset);
        return p->dt.alphaRef;
    }


    // DeclareProperty(KRenderState, uint8_t, pointSize);
    KRenderState::_pointSize& KRenderState::_pointSize::operator=(const uint8_t& other)
    {
        KRenderState::_pointSize* p1 = &((KRenderState*)0)->pointSize;
        size_t                    offset = (size_t)((char*)(p1));
        data* p = (data*)(this - offset);
        p->dt.pointSize = other;
        p->ZeroHash();
        return *this;
    }
    KRenderState::_pointSize::operator uint8_t()
    {
        KRenderState::_pointSize* p1 = &((KRenderState*)0)->pointSize;
        size_t                    offset = (size_t)((char*)(p1));
        data* p = (data*)(this - offset);
        return p->dt.pointSize;
    }

    // DeclareProperty(KRenderState, uint8_t, depthTestEnable);
    KRenderState::_depthTestEnable& KRenderState::_depthTestEnable::operator=(const uint8_t& other)
    {
        KRenderState::_depthTestEnable* p1 = &((KRenderState*)0)->depthTestEnable;
        size_t                          offset = (size_t)((char*)(p1));
        data* p = (data*)(this - offset);
        p->dt.depthTestEnable = other;
        p->ZeroHash();
        return *this;
    }
    KRenderState::_depthTestEnable::operator uint8_t()
    {
        KRenderState::_depthTestEnable* p1 = &((KRenderState*)0)->depthTestEnable;
        size_t                          offset = (size_t)((char*)(p1));
        data* p = (data*)(this - offset);
        return p->dt.depthTestEnable;
    }

    // DeclareProperty(KRenderState, uint8_t, depthWriteEnable);
    KRenderState::_depthWriteEnable& KRenderState::_depthWriteEnable::operator=(const uint8_t& other)
    {
        KRenderState::_depthWriteEnable* p1 = &((KRenderState*)0)->depthWriteEnable;
        size_t                           offset = (size_t)((char*)(p1));
        data* p = (data*)(this - offset);
        p->dt.depthWriteEnable = other;
        p->ZeroHash();
        return *this;
    }
    KRenderState::_depthWriteEnable::operator uint8_t()
    {
        KRenderState::_depthWriteEnable* p1 = &((KRenderState*)0)->depthWriteEnable;
        size_t                           offset = (size_t)((char*)(p1));
        data* p = (data*)(this - offset);
        return p->dt.depthWriteEnable;
    }

    // DeclareProperty(KRenderState, enumDepthType, depthCompareOp);
    KRenderState::_depthCompareOp& KRenderState::_depthCompareOp::operator=(const enumDepthType& other)
    {
        KRenderState::_depthCompareOp* p1 = &((KRenderState*)0)->depthCompareOp;
        size_t                         offset = (size_t)((char*)(p1));
        data* p = (data*)(this - offset);
        p->dt.depthCompareOp = other;
        p->ZeroHash();
        return *this;
    }
    KRenderState::_depthCompareOp::operator enumDepthType()
    {
        KRenderState::_depthCompareOp* p1 = &((KRenderState*)0)->depthCompareOp;
        size_t                         offset = (size_t)((char*)(p1));
        data* p = (data*)(this - offset);
        return p->dt.depthCompareOp;
    }


    // DeclareProperty(KRenderState, uint8_t, depthBoundsTestEnable);

    KRenderState::_depthBoundsTestEnable& KRenderState::_depthBoundsTestEnable::operator=(const uint8_t& other)
    {
        KRenderState::_depthBoundsTestEnable* p1 = &((KRenderState*)0)->depthBoundsTestEnable;
        size_t                                offset = (size_t)((char*)(p1));
        data* p = (data*)(this - offset);
        p->dt.depthBoundsTestEnable = other;
        p->ZeroHash();
        return *this;
    }
    KRenderState::_depthBoundsTestEnable::operator uint8_t()
    {
        KRenderState::_depthBoundsTestEnable* p1 = &((KRenderState*)0)->depthBoundsTestEnable;
        size_t                                offset = (size_t)((char*)(p1));
        data* p = (data*)(this - offset);
        return p->dt.depthBoundsTestEnable;
    }


    // DeclareProperty(KRenderState, uint8_t, stencilTestEnable);
    KRenderState::_stencilTestEnable& KRenderState::_stencilTestEnable::operator=(const uint8_t& other)
    {
        KRenderState::_stencilTestEnable* p1 = &((KRenderState*)0)->stencilTestEnable;
        size_t                            offset = (size_t)((char*)(p1));
        data* p = (data*)(this - offset);
        p->dt.stencilTestEnable = other;
        p->ZeroHash();
        return *this;
    }
    KRenderState::_stencilTestEnable::operator uint8_t()
    {
        KRenderState::_stencilTestEnable* p1 = &((KRenderState*)0)->stencilTestEnable;
        size_t                            offset = (size_t)((char*)(p1));
        data* p = (data*)(this - offset);
        return p->dt.stencilTestEnable;
    }


    // DeclareSecondProperty(KRenderState, uint32_t, stencilFront, compareMask);
    KRenderState::_stencilFront::_compareMask& KRenderState::_stencilFront::_compareMask::operator=(const uint32_t& other)
    {
        KRenderState::_stencilFront::_compareMask* p1 = &((KRenderState*)0)->stencilFront.compareMask;
        size_t                                     offset = (size_t)((char*)(p1));
        data* p = (data*)(this - offset);
        p->dt.stencilFront.compareMask = other;
        p->ZeroHash();
        return *this;
    }
    KRenderState::_stencilFront::_compareMask::operator uint32_t()
    {
        KRenderState::_stencilFront::_compareMask* p1 = &((KRenderState*)0)->stencilFront.compareMask;
        size_t                                     offset = (size_t)((char*)(p1));
        data* p = (data*)(this - offset);
        return p->dt.stencilFront.compareMask;
    }

    // DeclareSecondProperty(KRenderState, uint32_t, stencilFront, writeMask);
    KRenderState::_stencilFront::_writeMask& KRenderState::_stencilFront::_writeMask::operator=(const uint32_t& other)
    {
        KRenderState::_stencilFront::_writeMask* p1 = &((KRenderState*)0)->stencilFront.writeMask;
        size_t                                   offset = (size_t)((char*)(p1));
        data* p = (data*)(this - offset);
        p->dt.stencilFront.writeMask = other;
        p->ZeroHash();
        return *this;
    }
    KRenderState::_stencilFront::_writeMask::operator uint32_t()
    {
        KRenderState::_stencilFront::_writeMask* p1 = &((KRenderState*)0)->stencilFront.writeMask;
        size_t                                   offset = (size_t)((char*)(p1));
        data* p = (data*)(this - offset);
        return p->dt.stencilFront.writeMask;
    }


    // DeclareSecondProperty(KRenderState, short, stencilFront, reference);
    KRenderState::_stencilFront::_reference& KRenderState::_stencilFront::_reference::operator=(const short& other)
    {
        KRenderState::_stencilFront::_reference* p1 = &((KRenderState*)0)->stencilFront.reference;
        size_t                                   offset = (size_t)((char*)(p1));
        data* p = (data*)(this - offset);
        p->dt.stencilFront.reference = other;
        p->ZeroHash();
        return *this;
    }

    KRenderState::_stencilFront::_reference::operator short()
    {
        KRenderState::_stencilFront::_reference* p1 = &((KRenderState*)0)->stencilFront.reference;
        size_t                                   offset = (size_t)((char*)(p1));
        data* p = (data*)(this - offset);
        return p->dt.stencilFront.reference;
    }


    // DeclareSecondProperty(KRenderState, enumStencilType, stencilFront, stencilCompareOp);
    KRenderState::_stencilFront::_stencilCompareOp& KRenderState::_stencilFront::_stencilCompareOp::operator=(const enumStencilType& other)
    {
        KRenderState::_stencilFront::_stencilCompareOp* p1 = &((KRenderState*)0)->stencilFront.stencilCompareOp;
        size_t                                          offset = (size_t)((char*)(p1));
        data* p = (data*)(this - offset);
        p->dt.stencilFront.stencilCompareOp = other;
        p->ZeroHash();
        return *this;
    }
    KRenderState::_stencilFront::_stencilCompareOp::operator enumStencilType()
    {
        KRenderState::_stencilFront::_stencilCompareOp* p1 = &((KRenderState*)0)->stencilFront.stencilCompareOp;
        size_t                                          offset = (size_t)((char*)(p1));
        data* p = (data*)(this - offset);
        return p->dt.stencilFront.stencilCompareOp;
    }

    // DeclareSecondProperty(KRenderState, enumStencilOpType, stencilFront, sencilFailOp);
    KRenderState::_stencilFront::_sencilFailOp& KRenderState::_stencilFront::_sencilFailOp::operator=(const enumStencilOpType& other)
    {
        KRenderState::_stencilFront::_sencilFailOp* p1 = &((KRenderState*)0)->stencilFront.sencilFailOp;
        size_t                                      offset = (size_t)((char*)(p1));
        data* p = (data*)(this - offset);
        p->dt.stencilFront.sencilFailOp = other;
        p->ZeroHash();
        return *this;
    }
    KRenderState::_stencilFront::_sencilFailOp::operator enumStencilOpType()
    {
        KRenderState::_stencilFront::_sencilFailOp* p1 = &((KRenderState*)0)->stencilFront.sencilFailOp;
        size_t                                      offset = (size_t)((char*)(p1));
        data* p = (data*)(this - offset);
        return p->dt.stencilFront.sencilFailOp;
    }

    // DeclareSecondProperty(KRenderState, enumStencilOpType, stencilFront, stencilPassOp);
    KRenderState::_stencilFront::_stencilPassOp& KRenderState::_stencilFront::_stencilPassOp::operator=(const enumStencilOpType& other)
    {
        KRenderState::_stencilFront::_stencilPassOp* p1 = &((KRenderState*)0)->stencilFront.stencilPassOp;
        size_t                                       offset = (size_t)((char*)(p1));
        data* p = (data*)(this - offset);
        p->dt.stencilFront.stencilPassOp = other;
        p->ZeroHash();
        return *this;
    }
    KRenderState::_stencilFront::_stencilPassOp::operator enumStencilOpType()
    {
        KRenderState::_stencilFront::_stencilPassOp* p1 = &((KRenderState*)0)->stencilFront.stencilPassOp;
        size_t                                       offset = (size_t)((char*)(p1));
        data* p = (data*)(this - offset);
        return p->dt.stencilFront.stencilPassOp;
    }


    // DeclareSecondProperty(KRenderState, enumStencilOpType, stencilFront, stencilDepthFailOp);
    KRenderState::_stencilFront::_stencilDepthFailOp& KRenderState::_stencilFront::_stencilDepthFailOp::operator=(const enumStencilOpType& other)
    {
        KRenderState::_stencilFront::_stencilDepthFailOp* p1 = &((KRenderState*)0)->stencilFront.stencilDepthFailOp;
        size_t                                            offset = (size_t)((char*)(p1));
        data* p = (data*)(this - offset);
        p->dt.stencilFront.stencilDepthFailOp = other;
        p->ZeroHash();
        return *this;
    }

    KRenderState::_stencilFront::_stencilDepthFailOp::operator enumStencilOpType()
    {
        KRenderState::_stencilFront::_stencilDepthFailOp* p1 = &((KRenderState*)0)->stencilFront.stencilDepthFailOp;
        size_t                                            offset = (size_t)((char*)(p1));
        data* p = (data*)(this - offset);
        return p->dt.stencilFront.stencilDepthFailOp;
    }

    // DeclareSecondProperty(KRenderState, uint32_t, stencilBack, compareMask);
    KRenderState::_stencilBack::_compareMask& KRenderState::_stencilBack::_compareMask::operator=(const uint32_t& other)
    {
        KRenderState::_stencilBack::_compareMask* p1 = &((KRenderState*)0)->stencilBack.compareMask;
        size_t                                    offset = (size_t)((char*)(p1));
        data* p = (data*)(this - offset);
        p->dt.stencilBack.compareMask = other;
        p->ZeroHash();
        return *this;
    }

    KRenderState::_stencilBack::_compareMask::operator uint32_t()
    {
        KRenderState::_stencilBack::_compareMask* p1 = &((KRenderState*)0)->stencilBack.compareMask;
        size_t                                    offset = (size_t)((char*)(p1));
        data* p = (data*)(this - offset);
        return p->dt.stencilBack.compareMask;
    }


    // DeclareSecondProperty(KRenderState, uint32_t, stencilBack, writeMask);
    KRenderState::_stencilBack::_writeMask& KRenderState::_stencilBack::_writeMask::operator=(const uint32_t& other)
    {
        KRenderState::_stencilBack::_writeMask* p1 = &((KRenderState*)0)->stencilBack.writeMask;
        size_t                                  offset = (size_t)((char*)(p1));
        data* p = (data*)(this - offset);
        p->dt.stencilBack.writeMask = other;
        p->ZeroHash();
        return *this;
    }

    KRenderState::_stencilBack::_writeMask::operator uint32_t()
    {
        KRenderState::_stencilBack::_writeMask* p1 = &((KRenderState*)0)->stencilBack.writeMask;
        size_t                                  offset = (size_t)((char*)(p1));
        data* p = (data*)(this - offset);
        return p->dt.stencilBack.writeMask;
    }

    // DeclareSecondProperty(KRenderState, short, stencilBack, reference);
    KRenderState::_stencilBack::_reference& KRenderState::_stencilBack::_reference::operator=(const short& other)
    {
        KRenderState::_stencilBack::_reference* p1 = &((KRenderState*)0)->stencilBack.reference;
        size_t                                  offset = (size_t)((char*)(p1));
        data* p = (data*)(this - offset);
        p->dt.stencilBack.reference = other;
        p->ZeroHash();
        return *this;
    }
    KRenderState::_stencilBack::_reference::operator short()
    {
        KRenderState::_stencilBack::_reference* p1 = &((KRenderState*)0)->stencilBack.reference;
        size_t                                  offset = (size_t)((char*)(p1));
        data* p = (data*)(this - offset);
        return p->dt.stencilBack.reference;
    }

    // DeclareSecondProperty(KRenderState, enumStencilType, stencilBack, stencilCompareOp);
    KRenderState::_stencilBack::_stencilCompareOp& KRenderState::_stencilBack::_stencilCompareOp::operator=(const enumStencilType& other)
    {
        KRenderState::_stencilBack::_stencilCompareOp* p1 = &((KRenderState*)0)->stencilBack.stencilCompareOp;
        size_t                                         offset = (size_t)((char*)(p1));
        data* p = (data*)(this - offset);
        p->dt.stencilBack.stencilCompareOp = other;
        p->ZeroHash();
        return *this;
    }
    KRenderState::_stencilBack::_stencilCompareOp::operator enumStencilType()
    {
        KRenderState::_stencilBack::_stencilCompareOp* p1 = &((KRenderState*)0)->stencilBack.stencilCompareOp;
        size_t                                         offset = (size_t)((char*)(p1));
        data* p = (data*)(this - offset);
        return p->dt.stencilBack.stencilCompareOp;
    }

    // DeclareSecondProperty(KRenderState, enumStencilOpType, stencilBack, sencilFailOp);
    KRenderState::_stencilBack::_sencilFailOp& KRenderState::_stencilBack::_sencilFailOp::operator=(const enumStencilOpType& other)
    {
        KRenderState::_stencilBack::_sencilFailOp* p1 = &((KRenderState*)0)->stencilBack.sencilFailOp;
        size_t                                     offset = (size_t)((char*)(p1));
        data* p = (data*)(this - offset);
        p->dt.stencilBack.sencilFailOp = other;
        p->ZeroHash();
        return *this;
    }
    KRenderState::_stencilBack::_sencilFailOp::operator enumStencilOpType()
    {
        KRenderState::_stencilBack::_sencilFailOp* p1 = &((KRenderState*)0)->stencilBack.sencilFailOp;
        size_t                                     offset = (size_t)((char*)(p1));
        data* p = (data*)(this - offset);
        return p->dt.stencilBack.sencilFailOp;
    }


    // DeclareSecondProperty(KRenderState, enumStencilOpType, stencilBack, stencilPassOp);
    KRenderState::_stencilBack::_stencilPassOp& KRenderState::_stencilBack::_stencilPassOp::operator=(const enumStencilOpType& other)
    {
        KRenderState::_stencilBack::_stencilPassOp* p1 = &((KRenderState*)0)->stencilBack.stencilPassOp;
        size_t                                      offset = (size_t)((char*)(p1));
        data* p = (data*)(this - offset);
        p->dt.stencilBack.stencilPassOp = other;
        p->ZeroHash();
        return *this;
    }

    KRenderState::_stencilBack::_stencilPassOp::operator enumStencilOpType()
    {
        KRenderState::_stencilBack::_stencilPassOp* p1 = &((KRenderState*)0)->stencilBack.stencilPassOp;
        size_t                                      offset = (size_t)((char*)(p1));
        data* p = (data*)(this - offset);
        return p->dt.stencilBack.stencilPassOp;
    }


    // DeclareSecondProperty(KRenderState, enumStencilOpType, stencilBack, stencilDepthFailOp);
    KRenderState::_stencilBack::_stencilDepthFailOp& KRenderState::_stencilBack::_stencilDepthFailOp::operator=(const enumStencilOpType& other)
    {
        KRenderState::_stencilBack::_stencilDepthFailOp* p1 = &((KRenderState*)0)->stencilBack.stencilDepthFailOp;
        size_t                                           offset = (size_t)((char*)(p1));
        data* p = (data*)(this - offset);
        p->dt.stencilBack.stencilDepthFailOp = other;
        p->ZeroHash();
        return *this;
    }

    KRenderState::_stencilBack::_stencilDepthFailOp::operator enumStencilOpType()
    {
        KRenderState::_stencilBack::_stencilDepthFailOp* p1 = &((KRenderState*)0)->stencilBack.stencilDepthFailOp;
        size_t                                           offset = (size_t)((char*)(p1));
        data* p = (data*)(this - offset);
        return p->dt.stencilBack.stencilDepthFailOp;
    }


    KRenderState::_blendAttachment::_item::_item()
        : id(0)
    {
    }

    KRenderState::_blendAttachment::_item::_item(uint32_t i)
        : id(i)
    {
    }

    // DeclareArrayItemProperty(KRenderState, uint8_t, blendAttachment, blendEnable);
    KRenderState::_blendAttachment::_item::_blendEnable& KRenderState::_blendAttachment::_item::_blendEnable::operator=(const uint8_t& other)
    {
        uint32_t                                             id = *(uint32_t*)(this - &((_item*)0)->blendEnable);
        KRenderState::_blendAttachment::_item::_blendEnable* p1 = nullptr;
        p1 = &((KRenderState*)0)->blendAttachment.item[id].blendEnable;
        size_t offset = (size_t)((char*)(p1));
        data* p = (data*)(this - offset);
        p->dt.blendAttachment[id].blendEnable = other;
        p->ZeroHash();
        return *this;
    };
    KRenderState::_blendAttachment::_item::_blendEnable::operator uint8_t()
    {
        uint32_t                                             id = *(uint32_t*)(this - &((_item*)0)->blendEnable);
        KRenderState::_blendAttachment::_item::_blendEnable* p1 = nullptr;
        p1 = &((KRenderState*)0)->blendAttachment.item[id].blendEnable;
        size_t offset = (size_t)((char*)(p1));
        data* p = (data*)(this - offset);
        return p->dt.blendAttachment[id].blendEnable;
    }

    // DeclareArrayItemProperty(KRenderState, uint8_t, blendAttachment, writeR);
    KRenderState::_blendAttachment::_item::_writeR& KRenderState::_blendAttachment::_item::_writeR::operator=(const uint8_t& other)
    {
        uint32_t                                        id = *(uint32_t*)(this - &((_item*)0)->writeR);
        KRenderState::_blendAttachment::_item::_writeR* p1 = nullptr;
        p1 = &((KRenderState*)0)->blendAttachment.item[id].writeR;
        size_t offset = (size_t)((char*)(p1));
        data* p = (data*)(this - offset);
        p->dt.blendAttachment[id].writeR = other;
        p->ZeroHash();
        return *this;
    };
    KRenderState::_blendAttachment::_item::_writeR::operator uint8_t()
    {
        uint32_t                                        id = *(uint32_t*)(this - &((_item*)0)->writeR);
        KRenderState::_blendAttachment::_item::_writeR* p1 = nullptr;
        p1 = &((KRenderState*)0)->blendAttachment.item[id].writeR;
        size_t offset = (size_t)((char*)(p1));
        data* p = (data*)(this - offset);
        return p->dt.blendAttachment[id].writeR;
    }

    // DeclareArrayItemProperty(KRenderState, uint8_t, blendAttachment, writeG);
    KRenderState::_blendAttachment::_item::_writeG& KRenderState::_blendAttachment::_item::_writeG::operator=(const uint8_t& other)
    {
        uint32_t                                        id = *(uint32_t*)(this - &((_item*)0)->writeG);
        KRenderState::_blendAttachment::_item::_writeG* p1 = nullptr;
        p1 = &((KRenderState*)0)->blendAttachment.item[id].writeG;
        size_t offset = (size_t)((char*)(p1));
        data* p = (data*)(this - offset);
        p->dt.blendAttachment[id].writeG = other;
        p->ZeroHash();
        return *this;
    };
    KRenderState::_blendAttachment::_item::_writeG::operator uint8_t()
    {
        uint32_t                                        id = *(uint32_t*)(this - &((_item*)0)->writeG);
        KRenderState::_blendAttachment::_item::_writeG* p1 = nullptr;
        p1 = &((KRenderState*)0)->blendAttachment.item[id].writeG;
        size_t offset = (size_t)((char*)(p1));
        data* p = (data*)(this - offset);
        return p->dt.blendAttachment[id].writeG;
    }

    // DeclareArrayItemProperty(KRenderState, uint8_t, blendAttachment, writeB);
    KRenderState::_blendAttachment::_item::_writeB& KRenderState::_blendAttachment::_item::_writeB::operator=(const uint8_t& other)
    {
        uint32_t                                        id = *(uint32_t*)(this - &((_item*)0)->writeB);
        KRenderState::_blendAttachment::_item::_writeB* p1 = nullptr;
        p1 = &((KRenderState*)0)->blendAttachment.item[id].writeB;
        size_t offset = (size_t)((char*)(p1));
        data* p = (data*)(this - offset);
        p->dt.blendAttachment[id].writeB = other;
        p->ZeroHash();
        return *this;
    };
    KRenderState::_blendAttachment::_item::_writeB::operator uint8_t()
    {
        uint32_t                                        id = *(uint32_t*)(this - &((_item*)0)->writeB);
        KRenderState::_blendAttachment::_item::_writeB* p1 = nullptr;
        p1 = &((KRenderState*)0)->blendAttachment.item[id].writeB;
        size_t offset = (size_t)((char*)(p1));
        data* p = (data*)(this - offset);
        return p->dt.blendAttachment[id].writeB;
    }

    // DeclareArrayItemProperty(KRenderState, uint8_t, blendAttachment, writeA);
    KRenderState::_blendAttachment::_item::_writeA& KRenderState::_blendAttachment::_item::_writeA::operator=(const uint8_t& other)
    {
        uint32_t                                        id = *(uint32_t*)(this - &((_item*)0)->writeA);
        KRenderState::_blendAttachment::_item::_writeA* p1 = nullptr;
        p1 = &((KRenderState*)0)->blendAttachment.item[id].writeA;
        size_t offset = (size_t)((char*)(p1));
        data* p = (data*)(this - offset);
        p->dt.blendAttachment[id].writeA = other;
        p->ZeroHash();
        return *this;
    };
    KRenderState::_blendAttachment::_item::_writeA::operator uint8_t()
    {
        uint32_t                                        id = *(uint32_t*)(this - &((_item*)0)->writeA);
        KRenderState::_blendAttachment::_item::_writeA* p1 = nullptr;
        p1 = &((KRenderState*)0)->blendAttachment.item[id].writeA;
        size_t offset = (size_t)((char*)(p1));
        data* p = (data*)(this - offset);
        return p->dt.blendAttachment[id].writeA;
    }

    // DeclareArrayItemProperty(KRenderState, enumBlendType, blendAttachment, srcColorBlendFactor);
    KRenderState::_blendAttachment::_item::_srcColorBlendFactor& KRenderState::_blendAttachment::_item::_srcColorBlendFactor::operator=(const enumBlendType& other)
    {
        uint32_t                                                     id = *(uint32_t*)(this - &((_item*)0)->srcColorBlendFactor);
        KRenderState::_blendAttachment::_item::_srcColorBlendFactor* p1 = nullptr;
        p1 = &((KRenderState*)0)->blendAttachment.item[id].srcColorBlendFactor;
        size_t offset = (size_t)((char*)(p1));
        data* p = (data*)(this - offset);
        p->dt.blendAttachment[id].srcColorBlendFactor = other;
        p->ZeroHash();
        return *this;
    };
    KRenderState::_blendAttachment::_item::_srcColorBlendFactor::operator enumBlendType()
    {
        uint32_t                                                     id = *(uint32_t*)(this - &((_item*)0)->srcColorBlendFactor);
        KRenderState::_blendAttachment::_item::_srcColorBlendFactor* p1 = nullptr;
        p1 = &((KRenderState*)0)->blendAttachment.item[id].srcColorBlendFactor;
        size_t offset = (size_t)((char*)(p1));
        data* p = (data*)(this - offset);
        return p->dt.blendAttachment[id].srcColorBlendFactor;
    }

    // DeclareArrayItemProperty(KRenderState, enumBlendType, blendAttachment, dstColorBlendFactor);
    KRenderState::_blendAttachment::_item::_dstColorBlendFactor& KRenderState::_blendAttachment::_item::_dstColorBlendFactor::operator=(const enumBlendType& other)
    {
        uint32_t                                                     id = *(uint32_t*)(this - &((_item*)0)->dstColorBlendFactor);
        KRenderState::_blendAttachment::_item::_dstColorBlendFactor* p1 = nullptr;
        p1 = &((KRenderState*)0)->blendAttachment.item[id].dstColorBlendFactor;
        size_t offset = (size_t)((char*)(p1));
        data* p = (data*)(this - offset);
        p->dt.blendAttachment[id].dstColorBlendFactor = other;
        p->ZeroHash();
        return *this;
    };
    KRenderState::_blendAttachment::_item::_dstColorBlendFactor::operator enumBlendType()
    {
        uint32_t                                                     id = *(uint32_t*)(this - &((_item*)0)->dstColorBlendFactor);
        KRenderState::_blendAttachment::_item::_dstColorBlendFactor* p1 = nullptr;
        p1 = &((KRenderState*)0)->blendAttachment.item[id].dstColorBlendFactor;
        size_t offset = (size_t)((char*)(p1));
        data* p = (data*)(this - offset);
        return p->dt.blendAttachment[id].dstColorBlendFactor;
    }


    // DeclareArrayItemProperty(KRenderState, enumBlendType, blendAttachment, srcAlphaBlendFactor);
    KRenderState::_blendAttachment::_item::_srcAlphaBlendFactor& KRenderState::_blendAttachment::_item::_srcAlphaBlendFactor::operator=(const enumBlendType& other)
    {
        uint32_t                                                     id = *(uint32_t*)(this - &((_item*)0)->srcAlphaBlendFactor);
        KRenderState::_blendAttachment::_item::_srcAlphaBlendFactor* p1 = nullptr;
        p1 = &((KRenderState*)0)->blendAttachment.item[id].srcAlphaBlendFactor;
        size_t offset = (size_t)((char*)(p1));
        data* p = (data*)(this - offset);
        p->dt.blendAttachment[id].srcAlphaBlendFactor = other;
        p->ZeroHash();
        return *this;
    };
    KRenderState::_blendAttachment::_item::_srcAlphaBlendFactor::operator enumBlendType()
    {
        uint32_t                                                     id = *(uint32_t*)(this - &((_item*)0)->srcAlphaBlendFactor);
        KRenderState::_blendAttachment::_item::_srcAlphaBlendFactor* p1 = nullptr;
        p1 = &((KRenderState*)0)->blendAttachment.item[id].srcAlphaBlendFactor;
        size_t offset = (size_t)((char*)(p1));
        data* p = (data*)(this - offset);
        return p->dt.blendAttachment[id].srcAlphaBlendFactor;
    }


    // DeclareArrayItemProperty(KRenderState, enumBlendType, blendAttachment, dstAlphaBlendFactor);
    KRenderState::_blendAttachment::_item::_dstAlphaBlendFactor& KRenderState::_blendAttachment::_item::_dstAlphaBlendFactor::operator=(const enumBlendType& other)
    {
        uint32_t                                                     id = *(uint32_t*)(this - &((_item*)0)->dstAlphaBlendFactor);
        KRenderState::_blendAttachment::_item::_dstAlphaBlendFactor* p1 = nullptr;
        p1 = &((KRenderState*)0)->blendAttachment.item[id].dstAlphaBlendFactor;
        size_t offset = (size_t)((char*)(p1));
        data* p = (data*)(this - offset);
        p->dt.blendAttachment[id].dstAlphaBlendFactor = other;
        p->ZeroHash();
        return *this;
    };
    KRenderState::_blendAttachment::_item::_dstAlphaBlendFactor::operator enumBlendType()
    {
        uint32_t                                                     id = *(uint32_t*)(this - &((_item*)0)->dstAlphaBlendFactor);
        KRenderState::_blendAttachment::_item::_dstAlphaBlendFactor* p1 = nullptr;
        p1 = &((KRenderState*)0)->blendAttachment.item[id].dstAlphaBlendFactor;
        size_t offset = (size_t)((char*)(p1));
        data* p = (data*)(this - offset);
        return p->dt.blendAttachment[id].dstAlphaBlendFactor;
    }


    // DeclareArrayItemProperty(KRenderState, enumBlendEquationType, blendAttachment, colorBlendOp);
    KRenderState::_blendAttachment::_item::_colorBlendOp& KRenderState::_blendAttachment::_item::_colorBlendOp::operator=(const enumBlendEquationType& other)
    {
        uint32_t                                              id = *(uint32_t*)(this - &((_item*)0)->colorBlendOp);
        KRenderState::_blendAttachment::_item::_colorBlendOp* p1 = nullptr;
        p1 = &((KRenderState*)0)->blendAttachment.item[id].colorBlendOp;
        size_t offset = (size_t)((char*)(p1));
        data* p = (data*)(this - offset);
        p->dt.blendAttachment[id].colorBlendOp = other;
        p->ZeroHash();
        return *this;
    };
    KRenderState::_blendAttachment::_item::_colorBlendOp::operator enumBlendEquationType()
    {
        uint32_t                                              id = *(uint32_t*)(this - &((_item*)0)->colorBlendOp);
        KRenderState::_blendAttachment::_item::_colorBlendOp* p1 = nullptr;
        p1 = &((KRenderState*)0)->blendAttachment.item[id].colorBlendOp;
        size_t offset = (size_t)((char*)(p1));
        data* p = (data*)(this - offset);
        return p->dt.blendAttachment[id].colorBlendOp;
    }

    // DeclareArrayItemProperty(KRenderState, enumBlendEquationType, blendAttachment, alphaBlendOp);
    KRenderState::_blendAttachment::_item::_alphaBlendOp& KRenderState::_blendAttachment::_item::_alphaBlendOp::operator=(const enumBlendEquationType& other)
    {
        uint32_t                                              id = *(uint32_t*)(this - &((_item*)0)->alphaBlendOp);
        KRenderState::_blendAttachment::_item::_alphaBlendOp* p1 = nullptr;
        p1 = &((KRenderState*)0)->blendAttachment.item[id].alphaBlendOp;
        size_t offset = (size_t)((char*)(p1));
        data* p = (data*)(this - offset);
        p->dt.blendAttachment[id].alphaBlendOp = other;
        p->ZeroHash();
        return *this;
    };
    KRenderState::_blendAttachment::_item::_alphaBlendOp::operator enumBlendEquationType()
    {
        uint32_t                                              id = *(uint32_t*)(this - &((_item*)0)->alphaBlendOp);
        KRenderState::_blendAttachment::_item::_alphaBlendOp* p1 = nullptr;
        p1 = &((KRenderState*)0)->blendAttachment.item[id].alphaBlendOp;
        size_t offset = (size_t)((char*)(p1));
        data* p = (data*)(this - offset);
        return p->dt.blendAttachment[id].alphaBlendOp;
    }

    KRenderState::_blendAttachment::_item& KRenderState::_blendAttachment::operator[](int i)
    {
        return item[i];
    }

    KRenderState::_blendAttachment::_blendAttachment()
    {
        for (uint32_t i = 0; i < KMAX_BLEND_ATTACHMENT; ++i)
        {
            item[i].id = i;
        }
    }

    // DeclareProperty(KRenderState, uint8_t, defaultViewPortEnable);
    KRenderState::_defaultViewPortEnable& KRenderState::_defaultViewPortEnable::operator=(const uint8_t& other)
    {
        KRenderState::_defaultViewPortEnable* p1 = &((KRenderState*)0)->defaultViewPortEnable;
        size_t                                offset = (size_t)((char*)(p1));
        data* p = (data*)(this - offset);
        p->dt.defaultViewPortEnable = other;
        p->ZeroHash();
        return *this;
    }
    KRenderState::_defaultViewPortEnable::operator uint8_t()
    {
        KRenderState::_defaultViewPortEnable* p1 = &((KRenderState*)0)->defaultViewPortEnable;
        size_t                                offset = (size_t)((char*)(p1));
        data* p = (data*)(this - offset);
        return p->dt.defaultViewPortEnable;
    }


    // DeclareProperty(KRenderState, uint8_t, defaultScissorEnable);
    KRenderState::_defaultScissorEnable& KRenderState::_defaultScissorEnable::operator=(const uint8_t& other)
    {
        KRenderState::_defaultScissorEnable* p1 = &((KRenderState*)0)->defaultScissorEnable;
        size_t                               offset = (size_t)((char*)(p1));
        data* p = (data*)(this - offset);
        p->dt.defaultScissorEnable = other;
        p->ZeroHash();
        return *this;
    }
    KRenderState::_defaultScissorEnable::operator uint8_t()
    {
        KRenderState::_defaultScissorEnable* p1 = &((KRenderState*)0)->defaultScissorEnable;
        size_t                               offset = (size_t)((char*)(p1));
        data* p = (data*)(this - offset);
        return p->dt.defaultScissorEnable;
    }


    /////////////////////////////////////////////////////////////////////////////////////////////////
    KRenderState& KRenderState::operator=(const gfx::KRenderState& other)
    {
        memcpy(&d.dt, &other.d.dt, sizeof(gfx::KRenderState::data::_dt));
        d.ZeroHash();
        return *this;
    }


    int KRenderState::operator==(const gfx::KRenderState& other) const
    {
        if (this->GetHash() == other.GetHash())
        {
            return true;
        }
        else
        {
            return false;
        }
    }

    int KRenderState::operator!=(const gfx::KRenderState& other) const
    {
        if (this->GetHash() != other.GetHash())
        {
            return true;
        }
        else
        {
            return false;
        }
    }


    void KRenderState::ResetDefaultValue()
    {
        static KRenderState defaultValue;
        memcpy(&d.dt, &defaultValue.d.dt, sizeof(gfx::KRenderState::data::_dt));
        d.ZeroHash();
    }

    uint32_t KRenderState::GetHash() const
    {
        if (!d.hash)
        {
            d.hash = _GetHashCodeForMem32Bit((const char*)&d.dt, sizeof(d.dt));
        }
        return d.hash;
    }

    void KRenderState::data::ZeroHash()
    {
        hash = 0;
    }

    int KRenderState::SaveToMetaSection(KMetaSection* pSection)
    {
        static KRenderState defaultState;
        pSection->Clear();
        if (defaultState.d.dt.msaa != d.dt.msaa)
        {
            pSection->SetUint("msaa", d.dt.msaa);
        }

        if (defaultState.d.dt.msaa_line != d.dt.msaa_line)
        {
            pSection->SetUint("msaa_line", d.dt.msaa_line);
        }

        if (defaultState.d.dt.sampleShadingEnable != d.dt.sampleShadingEnable)
        {
            pSection->SetUint("sampleShadingEnable", d.dt.sampleShadingEnable);
        }

        if (defaultState.d.dt.sampleAlphaToCoverageEnable != d.dt.sampleAlphaToCoverageEnable)
        {
            pSection->SetUint("sampleAlphaToCoverageEnable", d.dt.sampleAlphaToCoverageEnable);
        }

        if (defaultState.d.dt.sampleAlphaToOneEnable != d.dt.sampleAlphaToOneEnable)
        {
            pSection->SetUint("sampleAlphaToOneEnable", d.dt.sampleAlphaToOneEnable);
        }

        if (defaultState.d.dt.sampleCountFlag != d.dt.sampleCountFlag)
        {
            pSection->SetUint("sampleCountFlag", d.dt.sampleCountFlag);
        }

        if (defaultState.d.dt.frontFaceMode != d.dt.frontFaceMode)
        {
            pSection->SetUint("frontFaceMode", d.dt.frontFaceMode);
        }

        if (defaultState.d.dt.cullMode != d.dt.cullMode)
        {
            pSection->SetUint("cullMode", d.dt.cullMode);
        }

        if (defaultState.d.dt.drawMode != d.dt.drawMode)
        {
            pSection->SetUint("drawMode", d.dt.drawMode);
        }

        if (defaultState.d.dt.polygonMode != d.dt.polygonMode)
        {
            pSection->SetUint("polygonMode", d.dt.polygonMode);
        }

        if (defaultState.d.dt.depthBiasEnable != d.dt.depthBiasEnable)
        {
            pSection->SetUint("depthBiasEnable", d.dt.depthBiasEnable);
        }

        if (defaultState.d.dt.depthBiasConstantFactor != d.dt.depthBiasConstantFactor)
        {
            pSection->SetFloat("depthBiasConstantFactor", d.dt.depthBiasConstantFactor);
        }

        if (defaultState.d.dt.depthBiasClamp != d.dt.depthBiasClamp)
        {
            pSection->SetFloat("depthBiasClamp", d.dt.depthBiasClamp);
        }

        if (defaultState.d.dt.depthBiasSlopeFactor != d.dt.depthBiasSlopeFactor)
        {
            pSection->SetFloat("depthBiasSlopeFactor", d.dt.depthBiasSlopeFactor);
        }

        if (defaultState.d.dt.rasterizerDiscardEnable != d.dt.rasterizerDiscardEnable)
        {
            pSection->SetUint("rasterizerDiscardEnable", d.dt.rasterizerDiscardEnable);
        }

        if (defaultState.d.dt.lineWidth != d.dt.lineWidth)
        {
            pSection->SetFloat("lineWidth", d.dt.lineWidth);
        }

        if (defaultState.d.dt.alphaRef != d.dt.alphaRef)
        {
            pSection->SetUint("alphaRef", d.dt.alphaRef);
        }

        if (defaultState.d.dt.pointSize != d.dt.pointSize)
        {
            pSection->SetUint("pointSize", d.dt.pointSize);
        }

        if (defaultState.d.dt.blendAttachCount != d.dt.blendAttachCount)
        {
            pSection->SetUint("blendAttachCount", d.dt.blendAttachCount);
        }
        for (int i = 0; i < KMAX_BLEND_ATTACHMENT; ++i)
        {
            char key[128];
            if (defaultState.d.dt.blendAttachment[i].blendEnable != d.dt.blendAttachment[i].blendEnable)
            {
                snprintf(key, 128, "blendAttachment%d_blendEnable", i);
                pSection->SetUint(key, d.dt.blendAttachment[i].blendEnable);
            }

            if (defaultState.d.dt.blendAttachment[i].writeA != d.dt.blendAttachment[i].writeA)
            {
                snprintf(key, 128, "blendAttachment%d_writeA", i);
                pSection->SetUint(key, d.dt.blendAttachment[i].writeA);
            }

            if (defaultState.d.dt.blendAttachment[i].writeR != d.dt.blendAttachment[i].writeR)
            {
                snprintf(key, 128, "blendAttachment%d_writeR", i);
                pSection->SetUint(key, d.dt.blendAttachment[i].writeR);
            }

            if (defaultState.d.dt.blendAttachment[i].writeG != d.dt.blendAttachment[i].writeG)
            {
                snprintf(key, 128, "blendAttachment%d_writeG", i);
                pSection->SetUint(key, d.dt.blendAttachment[i].writeG);
            }

            if (defaultState.d.dt.blendAttachment[i].writeB != d.dt.blendAttachment[i].writeB)
            {
                snprintf(key, 128, "blendAttachment%d_writeB", i);
                pSection->SetUint(key, d.dt.blendAttachment[i].writeB);
            }

            if (defaultState.d.dt.blendAttachment[i].srcColorBlendFactor != d.dt.blendAttachment[i].srcColorBlendFactor)
            {
                snprintf(key, 128, "blendAttachment%d_srcColorBlendFactor", i);
                pSection->SetUint(key, d.dt.blendAttachment[i].srcColorBlendFactor);
            }

            if (defaultState.d.dt.blendAttachment[i].dstColorBlendFactor != d.dt.blendAttachment[i].dstColorBlendFactor)
            {
                snprintf(key, 128, "blendAttachment%d_dstColorBlendFactor", i);
                pSection->SetUint(key, d.dt.blendAttachment[i].dstColorBlendFactor);
            }

            if (defaultState.d.dt.blendAttachment[i].colorBlendOp != d.dt.blendAttachment[i].colorBlendOp)
            {
                snprintf(key, 128, "blendAttachment%d_colorBlendOp", i);
                pSection->SetUint(key, d.dt.blendAttachment[i].colorBlendOp);
            }

            if (defaultState.d.dt.blendAttachment[i].srcAlphaBlendFactor != d.dt.blendAttachment[i].srcAlphaBlendFactor)
            {
                snprintf(key, 128, "blendAttachment%d_srcAlphaBlendFactor", i);
                pSection->SetUint(key, d.dt.blendAttachment[i].srcAlphaBlendFactor);
            }

            if (defaultState.d.dt.blendAttachment[i].dstAlphaBlendFactor != d.dt.blendAttachment[i].dstAlphaBlendFactor)
            {
                snprintf(key, 128, "blendAttachment%d_dstAlphaBlendFactor", i);
                pSection->SetUint(key, d.dt.blendAttachment[i].dstAlphaBlendFactor);
            }

            if (defaultState.d.dt.blendAttachment[i].alphaBlendOp != d.dt.blendAttachment[i].alphaBlendOp)
            {
                snprintf(key, 128, "blendAttachment%d_alphaBlendOp", i);
                pSection->SetUint(key, d.dt.blendAttachment[i].alphaBlendOp);
            }
        }

        if (defaultState.d.dt.depthTestEnable != d.dt.depthTestEnable)
        {
            pSection->SetUint("depthTestEnable", d.dt.depthTestEnable);
        }

        if (defaultState.d.dt.depthWriteEnable != d.dt.depthWriteEnable)
        {
            pSection->SetUint("depthWriteEnable", d.dt.depthWriteEnable);
        }

        if (defaultState.d.dt.depthCompareOp != d.dt.depthCompareOp)
        {
            pSection->SetUint("depthCompareOp", d.dt.depthCompareOp);
        }

        if (defaultState.d.dt.depthBoundsTestEnable != d.dt.depthBoundsTestEnable)
        {
            pSection->SetUint("depthBoundsTestEnable", d.dt.depthBoundsTestEnable);
        }

        if (defaultState.d.dt.minDepthBounds != d.dt.minDepthBounds)
        {
            pSection->SetFloat("minDepthBounds", d.dt.minDepthBounds);
        }

        if (defaultState.d.dt.maxDepthBounds != d.dt.maxDepthBounds)
        {
            pSection->SetFloat("maxDepthBounds", d.dt.maxDepthBounds);
        }

        if (defaultState.d.dt.stencilTestEnable != d.dt.stencilTestEnable)
        {
            pSection->SetFloat("stencilTestEnable", d.dt.stencilTestEnable);
        }


        if (defaultState.d.dt.stencilFront.compareMask != d.dt.stencilFront.compareMask)
        {
            pSection->SetUint("stencilFront_compareMask", d.dt.stencilFront.compareMask);
        }

        if (defaultState.d.dt.stencilFront.writeMask != d.dt.stencilFront.writeMask)
        {
            pSection->SetUint("stencilFront_writeMask", d.dt.stencilFront.writeMask);
        }

        if (defaultState.d.dt.stencilFront.reference != d.dt.stencilFront.reference)
        {
            pSection->SetUint("stencilFront_reference", d.dt.stencilFront.reference);
        }

        if (defaultState.d.dt.stencilFront.stencilCompareOp != d.dt.stencilFront.stencilCompareOp)
        {
            pSection->SetUint("stencilFront_stencilCompareOp", d.dt.stencilFront.stencilCompareOp);
        }

        if (defaultState.d.dt.stencilFront.sencilFailOp != d.dt.stencilFront.sencilFailOp)
        {
            pSection->SetUint("stencilFront_sencilFailOp", d.dt.stencilFront.sencilFailOp);
        }

        if (defaultState.d.dt.stencilFront.stencilPassOp != d.dt.stencilFront.stencilPassOp)
        {
            pSection->SetUint("stencilFront_stencilPassOp", d.dt.stencilFront.stencilPassOp);
        }

        if (defaultState.d.dt.stencilFront.stencilDepthFailOp != d.dt.stencilFront.stencilDepthFailOp)
        {
            pSection->SetUint("stencilFront_stencilDepthFailOp", d.dt.stencilFront.stencilDepthFailOp);
        }

        if (defaultState.d.dt.stencilBack.compareMask != d.dt.stencilBack.compareMask)
        {
            pSection->SetUint("stencilBack_compareMask", d.dt.stencilBack.compareMask);
        }

        if (defaultState.d.dt.stencilBack.writeMask != d.dt.stencilBack.writeMask)
        {
            pSection->SetUint("stencilBack_writeMask", d.dt.stencilBack.writeMask);
        }

        if (defaultState.d.dt.stencilBack.reference != d.dt.stencilBack.reference)
        {
            pSection->SetUint("stencilBack_reference", d.dt.stencilBack.reference);
        }

        if (defaultState.d.dt.stencilBack.stencilCompareOp != d.dt.stencilBack.stencilCompareOp)
        {
            pSection->SetUint("stencilBack_stencilCompareOp", d.dt.stencilBack.stencilCompareOp);
        }

        if (defaultState.d.dt.stencilBack.sencilFailOp != d.dt.stencilBack.sencilFailOp)
        {
            pSection->SetUint("stencilBack_sencilFailOp", d.dt.stencilBack.sencilFailOp);
        }

        if (defaultState.d.dt.stencilBack.stencilPassOp != d.dt.stencilBack.stencilPassOp)
        {
            pSection->SetUint("stencilBack_stencilPassOp", d.dt.stencilBack.stencilPassOp);
        }

        if (defaultState.d.dt.stencilBack.stencilDepthFailOp != d.dt.stencilBack.stencilDepthFailOp)
        {
            pSection->SetUint("stencilBack_stencilDepthFailOp", d.dt.stencilBack.stencilDepthFailOp);
        }

        if (defaultState.d.dt.defaultViewPortEnable != d.dt.defaultViewPortEnable)
        {
            pSection->SetUint("defaultViewPortEnable", d.dt.defaultViewPortEnable);
        }

        if (defaultState.d.dt.defaultScissorEnable != d.dt.defaultScissorEnable)
        {
            pSection->SetUint("defaultScissorEnable", d.dt.defaultScissorEnable);
        }

        return true;
    }

    int KRenderState::LoadFromMetaSction(KMetaSection* pSection)
    {
        // pSection->Clear();
        if (pSection->IsExist("msaa"))
        {
            uint32_t value = 0;
            pSection->GetUint("msaa", value);
            d.dt.msaa = (uint8_t)value;
        }

        if (pSection->IsExist("msaa_line"))
        {
            uint32_t value = 0;
            pSection->GetUint("msaa_line", value);
            d.dt.msaa_line = (uint8_t)value;
        }

        if (pSection->IsExist("sampleShadingEnable"))
        {
            uint32_t value = 0;
            pSection->GetUint("sampleShadingEnable", value);
            d.dt.sampleShadingEnable = (uint8_t)value;
        }

        if (pSection->IsExist("sampleAlphaToCoverageEnable"))
        {
            uint32_t value = 0;
            pSection->GetUint("sampleAlphaToCoverageEnable", value);
            d.dt.sampleAlphaToCoverageEnable = (uint8_t)value;
        }

        if (pSection->IsExist("sampleAlphaToOneEnable"))
        {
            uint32_t value = 0;
            pSection->GetUint("sampleAlphaToOneEnable", value);
            d.dt.sampleAlphaToOneEnable = (uint8_t)value;
        }

        if (pSection->IsExist("sampleCountFlag"))
        {
            uint32_t value = 0;
            pSection->GetUint("sampleCountFlag", value);
            d.dt.sampleCountFlag = (enumSampleCountFlag)value;
        }

        if (pSection->IsExist("frontFaceMode"))
        {
            uint32_t value = 0;
            pSection->GetUint("frontFaceMode", value);
            d.dt.frontFaceMode = (enumFrontFaceMode)value;
        }

        if (pSection->IsExist("cullMode"))
        {
            uint32_t value = 0;
            pSection->GetUint("cullMode", value);
            d.dt.cullMode = (enumCullMode)value;
        }


        if (pSection->IsExist("drawMode"))
        {
            uint32_t value = 0;
            pSection->GetUint("drawMode", value);
            d.dt.drawMode = (enumDrawMode)value;
        }

        if (pSection->IsExist("polygonMode"))
        {
            uint32_t value = 0;
            pSection->GetUint("polygonMode", value);
            d.dt.polygonMode = (enumPolygonMode)value;
        }

        if (pSection->IsExist("depthBiasEnable"))
        {
            uint32_t value = 0;
            pSection->GetUint("depthBiasEnable", value);
            d.dt.depthBiasEnable = (uint8_t)value;
        }

        if (pSection->IsExist("depthBiasConstantFactor"))
        {
            float value = 0;
            pSection->GetFloat("depthBiasConstantFactor", value);
            d.dt.depthBiasConstantFactor = value;
        }

        if (pSection->IsExist("depthBiasClamp"))
        {
            float value = 0;
            pSection->GetFloat("depthBiasClamp", value);
            d.dt.depthBiasClamp = value;
        }

        if (pSection->IsExist("depthBiasSlopeFactor"))
        {
            float value = 0;
            pSection->GetFloat("depthBiasSlopeFactor", value);
            d.dt.depthBiasSlopeFactor = value;
        }

        if (pSection->IsExist("rasterizerDiscardEnable"))
        {
            uint32_t value = 0;
            pSection->GetUint("rasterizerDiscardEnable", value);
            d.dt.rasterizerDiscardEnable = value;
        }

        if (pSection->IsExist("lineWidth"))
        {
            float value = 0;
            pSection->GetFloat("lineWidth", value);
            d.dt.lineWidth = value;
        }

        if (pSection->IsExist("alphaRef"))
        {
            uint32_t value = 0;
            pSection->GetUint("alphaRef", value);
            d.dt.alphaRef = (uint8_t)value;
        }

        if (pSection->IsExist("pointSize"))
        {
            uint32_t value = 0;
            pSection->GetUint("pointSize", value);
            d.dt.pointSize = (uint8_t)value;
        }

        if (pSection->IsExist("blendAttachCount"))
        {
            uint32_t value = 0;
            pSection->GetUint("blendAttachCount", value);
            d.dt.blendAttachCount = (uint8_t)value;
        }

        for (int i = 0; i < KMAX_BLEND_ATTACHMENT; ++i)
        {
            char key[128];

            sprintf(key, "blendAttachment%d_blendEnable", i);
            if (pSection->IsExist(key))
            {
                uint32_t value = 0;
                pSection->GetUint(key, value);
                d.dt.blendAttachment[i].blendEnable = (uint8_t)value;
            }

            sprintf(key, "blendAttachment%d_writeA", i);
            if (pSection->IsExist(key))
            {
                uint32_t value = 0;
                pSection->GetUint(key, value);
                d.dt.blendAttachment[i].writeA = (uint8_t)value;
            }


            sprintf(key, "blendAttachment%d_writeR", i);
            if (pSection->IsExist(key))
            {
                uint32_t value = 0;
                pSection->GetUint(key, value);
                d.dt.blendAttachment[i].writeR = (uint8_t)value;
            }

            sprintf(key, "blendAttachment%d_writeG", i);
            if (pSection->IsExist(key))
            {
                uint32_t value = 0;
                pSection->GetUint(key, value);
                d.dt.blendAttachment[i].writeG = (uint8_t)value;
            }

            sprintf(key, "blendAttachment%d_writeB", i);
            if (pSection->IsExist(key))
            {
                uint32_t value = 0;
                pSection->GetUint(key, value);
                d.dt.blendAttachment[i].writeB = (uint8_t)value;
            }

            sprintf(key, "blendAttachment%d_srcColorBlendFactor", i);
            if (pSection->IsExist(key))
            {
                uint32_t value = 0;
                pSection->GetUint(key, value);
                d.dt.blendAttachment[i].srcColorBlendFactor = (enumBlendType)value;
            }

            sprintf(key, "blendAttachment%d_dstColorBlendFactor", i);
            if (pSection->IsExist(key))
            {
                uint32_t value = 0;
                pSection->GetUint(key, value);
                d.dt.blendAttachment[i].dstColorBlendFactor = (enumBlendType)value;
            }


            sprintf(key, "blendAttachment%d_colorBlendOp", i);
            if (pSection->IsExist(key))
            {
                uint32_t value = 0;
                pSection->GetUint(key, value);
                d.dt.blendAttachment[i].colorBlendOp = (enumBlendEquationType)value;
            }

            sprintf(key, "blendAttachment%d_srcAlphaBlendFactor", i);
            if (pSection->IsExist(key))
            {
                uint32_t value = 0;
                pSection->GetUint(key, value);
                d.dt.blendAttachment[i].srcAlphaBlendFactor = (enumBlendType)value;
            }

            sprintf(key, "blendAttachment%d_dstAlphaBlendFactor", i);
            if (pSection->IsExist(key))
            {
                uint32_t value = 0;
                pSection->GetUint(key, value);
                d.dt.blendAttachment[i].dstAlphaBlendFactor = (enumBlendType)value;
            }

            sprintf(key, "blendAttachment%d_alphaBlendOp", i);
            if (pSection->IsExist(key))
            {
                uint32_t value = 0;
                pSection->GetUint(key, value);
                d.dt.blendAttachment[i].alphaBlendOp = (enumBlendEquationType)value;
            }
        }

        if (pSection->IsExist("depthTestEnable"))
        {
            uint32_t value = 0;
            pSection->GetUint("depthTestEnable", value);
            d.dt.depthTestEnable = (uint8_t)value;
        }

        if (pSection->IsExist("depthWriteEnable"))
        {
            uint32_t value = 0;
            pSection->GetUint("depthWriteEnable", value);
            d.dt.depthWriteEnable = (uint8_t)value;
        }

        if (pSection->IsExist("depthCompareOp"))
        {
            uint32_t value = 0;
            pSection->GetUint("depthCompareOp", value);
            d.dt.depthCompareOp = (enumDepthType)value;
        }

        if (pSection->IsExist("depthBoundsTestEnable"))
        {
            uint32_t value = 0;
            pSection->GetUint("depthCompareOp", value);
            d.dt.depthBoundsTestEnable = (uint8_t)value;
        }


        if (pSection->IsExist("minDepthBounds"))
        {
            float value = 0;
            pSection->GetFloat("minDepthBounds", value);
            d.dt.minDepthBounds = value;
        }

        if (pSection->IsExist("maxDepthBounds"))
        {
            float value = 0;
            pSection->GetFloat("maxDepthBounds", value);
            d.dt.maxDepthBounds = value;
        }

        if (pSection->IsExist("stencilTestEnable"))
        {
            uint32_t value = 0;
            pSection->GetUint("stencilTestEnable", value);
            d.dt.stencilTestEnable = value;
        }


        if (pSection->IsExist("stencilFront_compareMask"))
        {
            uint32_t value = 0;
            pSection->GetUint("stencilFront_compareMask", value);
            d.dt.stencilFront.compareMask = value;
        }

        if (pSection->IsExist("stencilFront_writeMask"))
        {
            uint32_t value = 0;
            pSection->GetUint("stencilFront_writeMask", value);
            d.dt.stencilFront.writeMask = value;
        }

        if (pSection->IsExist("stencilFront_reference"))
        {
            uint32_t value = 0;
            pSection->GetUint("stencilFront_reference", value);
            d.dt.stencilFront.reference = (short)value;
        }


        if (pSection->IsExist("stencilFront_stencilCompareOp"))
        {
            uint32_t value = 0;
            pSection->GetUint("stencilFront_stencilCompareOp", value);
            d.dt.stencilFront.stencilCompareOp = (enumStencilType)value;
        }

        if (pSection->IsExist("stencilFront_sencilFailOp"))
        {
            uint32_t value = 0;
            pSection->GetUint("stencilFront_sencilFailOp", value);
            d.dt.stencilFront.sencilFailOp = (enumStencilOpType)value;
        }

        if (pSection->IsExist("stencilFront_stencilPassOp"))
        {
            uint32_t value = 0;
            pSection->GetUint("stencilFront_stencilPassOp", value);
            d.dt.stencilFront.stencilPassOp = (enumStencilOpType)value;
        }

        if (pSection->IsExist("stencilFront_stencilDepthFailOp"))
        {
            uint32_t value = 0;
            pSection->GetUint("stencilFront_stencilDepthFailOp", value);
            d.dt.stencilFront.stencilDepthFailOp = (enumStencilOpType)value;
        }


        if (pSection->IsExist("stencilBack_compareMask"))
        {
            uint32_t value = 0;
            pSection->GetUint("stencilBack_compareMask", value);
            d.dt.stencilBack.compareMask = value;
        }

        if (pSection->IsExist("stencilBack_writeMask"))
        {
            uint32_t value = 0;
            pSection->GetUint("stencilBack_writeMask", value);
            d.dt.stencilBack.writeMask = value;
        }

        if (pSection->IsExist("stencilBack_reference"))
        {
            uint32_t value = 0;
            pSection->GetUint("stencilBack_reference", value);
            d.dt.stencilBack.reference = (short)value;
        }


        if (pSection->IsExist("stencilBack_stencilCompareOp"))
        {
            uint32_t value = 0;
            pSection->GetUint("stencilBack_stencilCompareOp", value);
            d.dt.stencilBack.stencilCompareOp = (enumStencilType)value;
        }

        if (pSection->IsExist("stencilBack_sencilFailOp"))
        {
            uint32_t value = 0;
            pSection->GetUint("stencilBack_sencilFailOp", value);
            d.dt.stencilBack.sencilFailOp = (enumStencilOpType)value;
        }

        if (pSection->IsExist("stencilBack_stencilPassOp"))
        {
            uint32_t value = 0;
            pSection->GetUint("stencilBack_stencilPassOp", value);
            d.dt.stencilBack.stencilPassOp = (enumStencilOpType)value;
        }

        if (pSection->IsExist("stencilBack_stencilDepthFailOp"))
        {
            uint32_t value = 0;
            pSection->GetUint("stencilBack_stencilDepthFailOp", value);
            d.dt.stencilBack.stencilDepthFailOp = (enumStencilOpType)value;
        }


        if (pSection->IsExist("defaultViewPortEnable"))
        {
            uint32_t value = 0;
            pSection->GetUint("defaultViewPortEnable", value);
            d.dt.defaultViewPortEnable = (uint8_t)value;
        }

        if (pSection->IsExist("defaultScissorEnable"))
        {
            uint32_t value = 0;
            pSection->GetUint("defaultScissorEnable", value);
            d.dt.defaultScissorEnable = (uint8_t)value;
        }
        d.ZeroHash();
        return true;
    }


    IKSharedPreBinder& KSharedPreBinder::BeginPreBind(BOOL bForce)
    {
        int32_t nFrame = NSEngine::GetRenderFrameMoveLoopCount();
        if (m_nFrameCount != nFrame || bForce)
        {
            m_nFrameCount = nFrame;
            //m_mapPreBindBuffer.clear();
            m_mapPreBindTexture.clear();
            m_mapPreBindBufferResourceView.clear();
            m_mapPreBindSampler.clear();
            m_mapPreBindTextures.clear();
            m_uPreBindBufferHash = 0;
        }
        m_bBegin = true;
        return *this;
    }

    //IKSharedPreBinder& KSharedPreBinder::PreBindBuffer(const_pool_str pName, gfx::IKGFX_Buffer* pBuffer)
    //{
    //    ASSERT(m_bBegin);
    //    if (m_bBegin && pBuffer)
    //    {
    //        m_mapPreBindBuffer.insert({pName, pBuffer});
    //    }
    //    return *this;
    //}

    IKSharedPreBinder& KSharedPreBinder::PreBindTexture(const_pool_str pName, gfx::IKGFX_TextureView* pTexture)
    {
        ASSERT(m_bBegin);
        if (m_bBegin && pTexture)
        {
            m_mapPreBindTexture.insert({ pName, pTexture });
        }
        return *this;
    }

    IKSharedPreBinder& KSharedPreBinder::PreBindBufferView(const_pool_str pName, gfx::IKGFX_BufferView* pBufView)
    {
        ASSERT(m_bBegin);
        if (m_bBegin && pBufView)
        {
            m_mapPreBindBufferResourceView.insert({ pName, pBufView });
        }
        return *this;
    }

    IKSharedPreBinder& KSharedPreBinder::PreBindTextures(const_pool_str pName, uint32_t uCount, gfx::IKGFX_TextureView* pTexture[])
    {
        ASSERT(m_bBegin);
        if (m_bBegin && uCount)
        {
            KBindingTextures binds;
            for (uint32_t i = 0; i < uCount; ++i)
            {
                if (pTexture[i])
                {
                    binds.vecTextures.emplace_back(pTexture[i]);
                }
            }
            if (!binds.vecTextures.empty())
            {
                m_mapPreBindTextures.insert({ pName, binds });
            }
        }
        return *this;
    }

    IKSharedPreBinder& KSharedPreBinder::PreBindSampler(const_pool_str pName, gfx::IKGFX_Sampler* pSampler)
    {
        ASSERT(m_bBegin);
        if (m_bBegin && pSampler)
        {
            m_mapPreBindSampler.insert({ pName, pSampler });
        }
        return *this;
    }

    BOOL KSharedPreBinder::EndPreBind()
    {
        if (m_bBegin)
        {
            //for (auto it : m_mapPreBindBuffer)
            //{
            //    gfx::IKGFX_Buffer* pBuffer  = it.second;
            //    m_uPreBindBufferHash     += ((uint64_t)it.first ^ it.second->GetCode());
            //}
            KSTR_HELPER::U64Hash u64hash;
            for (auto it : m_mapPreBindTexture)
            {
                //m_uPreBindBufferHash += ((uint64_t)it.first ^ ((KVulkanTextureView*)it.second)->GetCode());
                u64hash.Encode((uint64_t)it.first ^ (uint64_t)(it.second->GetNativeHandle()));
            }

            for (auto& it : m_mapPreBindTextures)
            {
                KBindingTextures& binds = it.second;
                for (auto& itt : binds.vecTextures)
                {
                    //m_uPreBindBufferHash += ((uint64_t)it.first ^ ((KVulkanTextureView*)itt)->GetCode());
                    u64hash.Encode((uint64_t)it.first ^ (uint64_t)(itt->GetNativeHandle()));
                }
            }

            for (auto& it : m_mapPreBindBufferResourceView)
            {
                u64hash.Encode((uint64_t)it.first ^ (uint64_t)(it.second->GetNativeHandle()));
            }

            for (auto& it : m_mapPreBindSampler)
            {
                gfx::IKGFX_Sampler* binds = it.second;
                //m_uPreBindBufferHash      += ((uint64_t)it.first ^ it.second->GetNativeHandle());
                u64hash.Encode((uint64_t)it.first ^ it.second->GetNativeHandle());
            }
            m_uPreBindBufferHash += u64hash.GetHash();
        }
        return true;
    }

    uint64_t KSharedPreBinder::GetHash() const
    {
        return m_uPreBindBufferHash;
    }


    //gfx::IKGFX_Buffer* KSharedPreBinder::GetPreBindBuffer(const_pool_str pName)
    //{
    //    gfx::IKGFX_Buffer* pGfxBuffer = nullptr;
    //    auto             it         = m_mapPreBindBuffer.find(pName);
    //    if (it != m_mapPreBindBuffer.end())
    //    {
    //        pGfxBuffer = it->second;
    //    }
    //    return pGfxBuffer;
    //}

    gfx::IKGFX_TextureView* KSharedPreBinder::GetPreBindTexture(const_pool_str pName)
    {
        gfx::IKGFX_TextureView* pTexture = nullptr;
        auto                    it = m_mapPreBindTexture.find(pName);
        if (it != m_mapPreBindTexture.end())
        {
            pTexture = it->second;
        }
        return pTexture;
    }

    gfx::KBindingTextures* KSharedPreBinder::GetPreBindTextures(const_pool_str pName)
    {
        gfx::KBindingTextures* pBinds = nullptr;
        auto                   it = m_mapPreBindTextures.find(pName);
        if (it != m_mapPreBindTextures.end())
        {
            pBinds = &it->second;
        }
        return pBinds;
    }

    gfx::IKGFX_BufferView* KSharedPreBinder::GetPreBindBufferView(const_pool_str pName)
    {
        gfx::IKGFX_BufferView* pBindResourceView = nullptr;
        auto                   it = m_mapPreBindBufferResourceView.find(pName);
        if (it != m_mapPreBindBufferResourceView.end())
        {
            pBindResourceView = it->second;
        }
        return pBindResourceView;
    }

    gfx::IKGFX_Sampler* KSharedPreBinder::GetPreBindSampler(const_pool_str pName)
    {
        gfx::IKGFX_Sampler* pSampler = nullptr;
        auto                it = m_mapPreBindSampler.find(pName);
        if (it != m_mapPreBindSampler.end())
        {
            pSampler = it->second;
        }
        return pSampler;
    }

    uint32_t gfx::GetBaseTypeSize(gfx::enumUniformBaseType b)
    {
        uint32_t sz = 4;
        switch (b)
        {
        case gfx::BASE_FLOAT:
        case gfx::BASE_INT:
        case gfx::BASE_BOOL:
        case gfx::BASE_UINT:
            sz = 4;
            break;
        case gfx::BASE_DOUBLE:
        case gfx::BASE_INT64:
        case gfx::BASE_UINT64:
            sz = 8;
            break;
        default:
            break;
        }
        return sz;
    }

    uint32_t gfx::GetProgramDataTypeSize(gfx::enumProgramDataType t, uint32_t uArrayCount)
    {
        uint32_t sz = 0;
        switch (t)
        {
        case gfx::FLOAT1_TYPE:
            sz = sizeof(float);
            break;
        case gfx::FLOAT2_TYPE:
            sz = sizeof(float) * 2;
            break;
        case gfx::FLOAT3_TYPE:
            sz = sizeof(float) * 3;
            break;
        case gfx::FLOAT4_TYPE:
            sz = sizeof(float) * 4;
            break;
        case gfx::FLOAT4X4_TYPE:
            sz = sizeof(float) * 16;
            break;
        case gfx::FLOAT1_ARRAY_TYPE:
            sz = sizeof(float) * uArrayCount;
            break;
        case gfx::FLOAT2_ARRAY_TYPE:
            sz = sizeof(float) * 2 * uArrayCount;
            break;
        case gfx::FLOAT3_ARRAY_TYPE:
            sz = sizeof(float) * 3 * uArrayCount;
            break;
        case gfx::FLOAT4_ARRAY_TYPE:
            sz = sizeof(float) * 4 * uArrayCount;
            break;
        case gfx::FLOAT4X4_ARRAY_TYPE:
            sz = sizeof(float) * 16 * uArrayCount;
            break;
        case gfx::INT1_TYPE:
        case gfx::UINT1_TYPE:
            sz = sizeof(int);
            break;
        case gfx::INT2_TYPE:
        case gfx::UINT2_TYPE:
            sz = sizeof(int) * 2;
            break;
        case gfx::INT3_TYPE:
        case gfx::UINT3_TYPE:
            sz = sizeof(int) * 3;
            break;
        case gfx::INT4_TYPE:
        case gfx::UINT4_TYPE:
            sz = sizeof(int) * 4;
            break;
        case gfx::INT_ARRAY_TYPE:
        case gfx::UINT_ARRAY_TYPE:
            sz = sizeof(int) * uArrayCount;
            break;
        case gfx::IMAGE_TEXTURE:
            break;
        case gfx::USER_TYPE:
            break;
        default:
            break;
        }
        return sz;
    }

    gfx::enumVertexFormat gfx::GetVertFormat(gfx::enumProgramDataType t, gfx::KAttribUsage::Enum usage)
    {
        gfx::enumVertexFormat fmt = gfx::VERT_FORMAT_COUNT;
        switch (t)
        {
        case gfx::FLOAT1_TYPE:
            fmt = gfx::VERT_FORMAT_R32_SFLOAT;
            break;
        case gfx::FLOAT2_TYPE:
            fmt = gfx::VERT_FORMAT_R32G32_SFLOAT;
            break;
        case gfx::FLOAT3_TYPE:
            fmt = gfx::VERT_FORMAT_R32G32B32_SFLOAT;
            break;
        case gfx::FLOAT4_TYPE:
            if (usage == gfx::KAttribUsage::VERT_COLOR_INDX)
            {
                fmt = gfx::VERT_FORMAT_R8G8B8A8_UNORM;
            }
            else
            {
                fmt = gfx::VERT_FORMAT_R32G32B32A32_SFLOAT;
            }
            break;
        case gfx::FLOAT4X4_TYPE:
            break;
        case gfx::FLOAT1_ARRAY_TYPE:
            break;
        case gfx::FLOAT2_ARRAY_TYPE:
            break;
        case gfx::FLOAT3_ARRAY_TYPE:
            break;
        case gfx::FLOAT4_ARRAY_TYPE:
            break;
        case gfx::FLOAT4X4_ARRAY_TYPE:
            break;
        case gfx::INT1_TYPE:
            break;
        case gfx::INT2_TYPE:
        {
            if (usage == gfx::KAttribUsage::VERT_TEX0_INDX || usage == gfx::KAttribUsage::VERT_TEX1_INDX)
            {
                fmt = gfx::VERT_FORMAT_R16G16_SINT;
            }
        }
        break;
        case gfx::UINT2_TYPE:
        {
            if (usage == gfx::KAttribUsage::VERT_TEX0_INDX || usage == gfx::KAttribUsage::VERT_TEX1_INDX)
            {
                fmt = gfx::VERT_FORMAT_R16G16_UINT;
            }
        }
        break;
        case gfx::INT3_TYPE:
            break;
        case gfx::INT4_TYPE:
            break;
        case gfx::INT_ARRAY_TYPE:
            break;
        case gfx::IMAGE_TEXTURE:
            break;
        case gfx::USER_TYPE:
            break;
        default:
            break;
        }
        if (fmt == gfx::VERT_FORMAT_COUNT)
        {
            KGLogPrintf(KGLOG_ERR, "error vert fmt");
        }
        return fmt;
    }

    /*KRenderPassKey::KRenderPassKey(KEnumRenderPass passtype)
    {
        eRenderPass  = 0;
        cGBufferMask = 0;
        not_used     = 0;
        if (passtype == RENDER_PASS_SCREEN_OFFSET_MAIN_CAMREA) //passtype == gfx::RENDER_PASS_SSS_COMBINE ||
        {
            KEngineOptions* pSwitchOption = NSEngine::GetEngineOptions();
            char            mask          = pSwitchOption->cGBufferMask;
            cGBufferMask =
                (mask & (char)MRTMask::Normal) |
                (mask & (char)MRTMask::Albedo) |
                (mask & (char)MRTMask::MotionVector) |
                (mask & (char)MRTMask::SunLight);
        }
    }
    */

    bool KSamplerState::operator==(const KSamplerState& other) const
    {
        return fMipLodBias == other.fMipLodBias && finialMipBias == other.finialMipBias &&
            fMaxAnisotropy == other.fMaxAnisotropy && fToMinLod == other.fToMinLod && fToMaxLod == other.fToMaxLod && u == other.u;
    }

    bool KSamplerState::operator<(const KSamplerState& other) const
    {
        if (fMipLodBias != other.fMipLodBias)
            return fMipLodBias < other.fMipLodBias;
        if (finialMipBias != other.finialMipBias)
            return finialMipBias < other.finialMipBias;
        if (fMaxAnisotropy != other.fMaxAnisotropy)
            return fMaxAnisotropy < other.fMaxAnisotropy;
        if (fMaxAnisotropy != other.fMaxAnisotropy)
            return fMaxAnisotropy < other.fMaxAnisotropy;
        if (fToMinLod != other.fToMinLod)
            return fToMinLod < other.fToMinLod;
        if (fToMaxLod != other.fToMaxLod)
            return fToMaxLod < other.fToMaxLod;
        if (u != other.u)
            return u < other.u;

        return false; // All fields are equal
    }

    KSamplerState::KSamplerState()
    {
        u = 0;
        bCompareEnable = false;
        enuCompareFunc = SAMPLER_COMPARE_OP_NEVER;
        fMipLodBias = 0.0f;
        finialMipBias = 0.0f;
        fMaxAnisotropy = 0.0f;
        fToMinLod = 0.0f;
        fToMaxLod = 1000.f; // VK_LOD_CLAMP_NONE
        bEnableMipmap = true;
        enuMagFilter = FILTER_LINEAR;
        enuMinFilter = FILTER_LINEAR;
        enuAddressModeU = SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        enuAddressModeV = SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        enuAddressModeW = SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        enuMipmapMode = SAMPLER_MIPMAP_MODE_LINEAR;
        enuBorderColor = BORDER_COLOR_FLOAT_OPAQUE_BLACK;
        enuTextureReductionOp = enumTextureReductionOp::Average;
        bNeedShaderInit = false;
    }

    KSamplerState::KSamplerState(enumSamplerFilter minFilter, enumSamplerFilter magFilter, enumMipMapMode mipMapMode, enumSamplerAddressMode enuAddressModeU, enumSamplerAddressMode enuAddressModeV, enumSamplerAddressMode enuAddressModeW, float maxAnisotropy, enumBorderColor borderColor, enumTextureReductionOp textureReductionOp, enumSamplerCompareFunc func)
    {
        u = 0;
        bCompareEnable = func != SAMPLER_COMPARE_OP_NEVER;
        enuCompareFunc = func;
        fMipLodBias = 0.0f;
        finialMipBias = 0.0f;
        fMaxAnisotropy = maxAnisotropy;
        fToMinLod = 0.0f;
        fToMaxLod = 1000.f; // VK_LOD_CLAMP_NONE
        bEnableMipmap = true;
        enuMagFilter = magFilter;
        enuMinFilter = minFilter;
        this->enuAddressModeU = enuAddressModeU;
        this->enuAddressModeV = enuAddressModeV;
        this->enuAddressModeW = enuAddressModeW;
        enuMipmapMode = mipMapMode;
        enuBorderColor = borderColor;
        enuTextureReductionOp = textureReductionOp;
        bNeedShaderInit = false;
    }

    const_pool_str KSamplerState::GetKey()
    {
        //if (m_pKey == nullptr || m_fMipLodBias != fMipLodBias || m_fMaxAnisotropy != fMaxAnisotropy || m_fToMinLod != fToMinLod || m_fToMaxLod != fToMaxLod || m_u != u)
        if (m_pKey == nullptr || memcmp(&m_fMipLodBias, &fMipLodBias, sizeof(float) * 6) != 0)
        {
            char key[64];
            memcpy(&m_fMipLodBias, &fMipLodBias, sizeof(float) * 6);
            //m_fMipLodBias = fMipLodBias;
            //m_finialMipBias = finialMipBias;
            //m_fMaxAnisotropy = fMaxAnisotropy;
            //m_fToMinLod = fToMinLod;
            //m_fToMaxLod = fToMaxLod;
            //m_u = u;
            snprintf(key, 64, "%.3f_%.3f_%.3f_%.3f_%u", fMipLodBias, fMaxAnisotropy, fToMinLod, fToMaxLod, u);
            m_pKey = GetParamNameByPool(key);
        }
        return m_pKey;
    }


    int32_t IKGFX_Program::Release()
    {
        m_nRef--;
        int32_t nRef = m_nRef;
        if (nRef == 0)
        {
            IKGFX_Program* p = this;
            BOOL           bRet = KGFX_GetGraphicDevice()->DestroyProgram(p);
            // SAFE_DELETE(p);
            ASSERT(bRet);
        }
        return nRef;
    }

    KRayTracingUniformBlockInfo::~KRayTracingUniformBlockInfo()
    {
        for (auto& iter : m_mapUnifroms)
        {
            SAFE_DELETE(iter.second);
        }
        m_mapUnifroms.clear();
    }
    KRayTracingUniformInfo* KRayTracingUniformBlockInfo::GetUnifromByName(const char* name)
    {
        const_pool_str regBindlessName = GetParamNameByPool(name);
        auto iter = m_mapUnifroms.find(regBindlessName);
        if (iter != m_mapUnifroms.end())
        {
            return iter->second;
        }
        else
        {
            return nullptr;
        }
    }
    bool KRayTracingProgram::Create(const RayTracingProgramDesc& rtpDC)
    {
        bool bRetCode = false;
        bool bResult = false;
        const std::vector<IRayTracingShader*>& vecHitShaders = rtpDC.vecHitShaders;
        const std::vector<IRayTracingShader*>& vecMissShaders = rtpDC.vecMissShaders;
        const std::vector<IRayTracingShader*>& vecRayGenShaders = rtpDC.vecRayGenShaders;
        const std::vector<IRayTracingShader*>& vecCallableShaders = rtpDC.vecCallableShaders;
        uint32_t l_uIndex = 0;
        for (auto pShader : vecHitShaders)
        {
            uint64_t uHash = pShader->GetHash();
            auto iter = m_mapHitGroupHashToIndex.find(uHash);
            KG_PROCESS_ERROR(iter == m_mapHitGroupHashToIndex.end());//error : same shader added into program
            m_mapHitGroupHashToIndex.emplace(uHash, l_uIndex++);
        }
        l_uIndex = 0;
        for (auto pShader : vecMissShaders)
        {
            uint64_t uHash = pShader->GetHash();
            auto iter = m_mapMissShaderHashToIndex.find(uHash);
            KG_PROCESS_ERROR(iter == m_mapMissShaderHashToIndex.end());//error : same shader added into program
            m_mapMissShaderHashToIndex.emplace(uHash, l_uIndex++);
        }
        l_uIndex = 0;
        for (auto pShader : vecRayGenShaders)
        {
            uint64_t uHash = pShader->GetHash();
            auto iter = m_mapRayGenShaderHashToIndex.find(uHash);
            KG_PROCESS_ERROR(iter == m_mapRayGenShaderHashToIndex.end());//error : same shader added into program
            m_mapRayGenShaderHashToIndex.emplace(uHash, l_uIndex++);
        }
        l_uIndex = 0;
        for (auto pShader : vecCallableShaders)
        {
            uint64_t uHash = pShader->GetHash();
            auto iter = m_mapCallableShaderHashToIndex.find(uHash);
            KG_PROCESS_ERROR(iter == m_mapCallableShaderHashToIndex.end());//error : same shader added into program
            m_mapCallableShaderHashToIndex.emplace(uHash, l_uIndex++);
        }
        bResult = true;
    Exit0:
        return bResult;
    }

    void KRayTracingProgram::Destroy()
    {
        m_mapHitGroupHashToIndex.clear();
        m_mapCallableShaderHashToIndex.clear();
        m_mapMissShaderHashToIndex.clear();
        m_mapRayGenShaderHashToIndex.clear();
    }

    uint32_t KRayTracingProgram::GetRayTracingHitGroupIndex(IRayTracingShader* pShader)
    {
        uint32_t uRetIndex = UINT32_MAX;
        if (pShader)
        {
            auto iter = m_mapHitGroupHashToIndex.find(pShader->GetHash());
            if (iter != m_mapHitGroupHashToIndex.end())
            {
                uRetIndex = iter->second;
            }
        }
        return uRetIndex;
    }
    uint32_t KRayTracingProgram::GetRayTracingMissShaderIndex(IRayTracingShader* pShader)
    {
        uint32_t uRetIndex = UINT32_MAX;
        if (pShader)
        {
            auto iter = m_mapMissShaderHashToIndex.find(pShader->GetHash());
            if (iter != m_mapMissShaderHashToIndex.end())
            {
                uRetIndex = iter->second;
            }
        }
        return uRetIndex;
    }
    uint32_t KRayTracingProgram::GetRayTracingCallableShaderIndex(IRayTracingShader* pShader)
    {
        uint32_t uRetIndex = UINT32_MAX;
        if (pShader)
        {
            auto iter = m_mapCallableShaderHashToIndex.find(pShader->GetHash());
            if (iter != m_mapCallableShaderHashToIndex.end())
            {
                uRetIndex = iter->second;
            }
        }
        return uRetIndex;
    }
    uint32_t KRayTracingProgram::GetRayTracingRayGenShaderIndex(IRayTracingShader* pShader)
    {
        uint32_t uRetIndex = UINT32_MAX;
        if (pShader)
        {
            auto iter = m_mapRayGenShaderHashToIndex.find(pShader->GetHash());
            if (iter != m_mapRayGenShaderHashToIndex.end())
            {
                uRetIndex = iter->second;
            }
        }
        return uRetIndex;
    }
    uint32_t KRayTracingProgram::GetRayTracingShaderIndexInPipeline(IRayTracingShader* pShader)
    {
        uint32_t retIndex = UINT32_MAX;
        switch (pShader->GetType())
        {
        case gfx::enumRayTracingShaderType::KRT_ST_RAY_GEN:
            retIndex = GetRayTracingRayGenShaderIndex(pShader);
            break;
        case gfx::enumRayTracingShaderType::KRT_ST_HIT_GROUP:
            retIndex = GetRayTracingHitGroupIndex(pShader);
            break;
        case gfx::enumRayTracingShaderType::KRT_ST_MISS:
            retIndex = GetRayTracingMissShaderIndex(pShader);
            break;
        case gfx::enumRayTracingShaderType::KRT_ST_CALLABLE:
            retIndex = GetRayTracingCallableShaderIndex(pShader);
            break;
        default:
            break;
        }
        return retIndex;
    }

    KGFX_TextureDesc KGFX_TextureDesc::g_EmptryValue{};

    namespace RayTracingHelper
    {
        gfx::ShaderStageType GetGfxShaderStageTypeFromSubShaderType(ERTShaderSubType subType)
        {
            gfx::ShaderStageType sRetType = ShaderStageType::AllGraphics;
            switch (subType)
            {
            case gfx::E_RT_TYPE_CLOSEST_HIT:
                sRetType = ShaderStageType::ClosestHit;
                break;
            case gfx::E_RT_TYPE_ANY_HIT:
                sRetType = ShaderStageType::AnyHit;
                break;
            case gfx::E_RT_TYPE_INTERSECTION:
                sRetType = ShaderStageType::Intersection;
                break;
            default:
                break;
            }
            return sRetType;
        }

        gfx::ShaderStageType GetGfxShaderStageTypeFromRTShaderType(enumRayTracingShaderType sShaderType, ERTShaderSubType subType)
        {
            gfx::ShaderStageType sRetType = ShaderStageType::AllGraphics;
            switch (sShaderType)
            {
            case gfx::KRT_ST_RAY_GEN:
                sRetType = ShaderStageType::RayGeneration;
                break;
            case gfx::KRT_ST_HIT_GROUP:
                sRetType = GetGfxShaderStageTypeFromSubShaderType(subType);
                break;
            case gfx::KRT_ST_MISS:
                sRetType = ShaderStageType::Miss;
                break;
            case gfx::KRT_ST_CALLABLE:
                sRetType = ShaderStageType::Callable;
                break;
            case gfx::KRT_ST_MAX_ENUM:
                break;
            default:
                break;
            }
            return sRetType;
        }
    }

    enumTextureFormat g_eDefaultDepthStencilFormat = enumTextureFormat::TEX_FORMAT_NONE;
    void InitDefaultDepthStencilFormat()
    {
        ASSERT(g_eDefaultDepthStencilFormat == enumTextureFormat::TEX_FORMAT_NONE);
        if (g_eDefaultDepthStencilFormat != enumTextureFormat::TEX_FORMAT_NONE)
        {
            KGLogPrintf(KGLOG_ERR, "[Fatal] g_eDefaultDepthStencilFormat inited twice");
            return;
        }
#if defined(__APPLE__) || defined(_WIN32)
        g_eDefaultDepthStencilFormat = ((DrvOption::GetRenderApi() == GFX_API::GFX_VULKAN_API) ? gfx::TEX_FORMAT_D32_SFLOAT_S8_UINT : gfx::TEX_FORMAT_D24_UNORM_S8_UINT);
#else
        g_eDefaultDepthStencilFormat = (DrvOption::bSupportD24S8 ? gfx::TEX_FORMAT_D24_UNORM_S8_UINT : gfx::TEX_FORMAT_D32_SFLOAT_S8_UINT);
#endif
    }

    enumTextureFormat GetDefaultDepthStencilFormat()
    {
        ASSERT(g_eDefaultDepthStencilFormat != enumTextureFormat::TEX_FORMAT_NONE);
        if (g_eDefaultDepthStencilFormat == enumTextureFormat::TEX_FORMAT_NONE)
        {
            KGLogPrintf(KGLOG_ERR, "[Fatal] g_eDefaultDepthStencilFormat get before init");
        }
        return g_eDefaultDepthStencilFormat;
    }

#if DESCRIPTORSET_VALIDATE
    std::atomic<uint32_t> gfx::KGfxRef::RefSequenceCounter = 0;
#endif
} // namespace gfx

