#include "pch.h"
/*
    filename:       rcrenderer_dx11_texture2d.cpp
    author:         Ming Dong
    date:           2016-MAY-18
    description:    
*/

#include "rcrenderer_dx11.h"
#include "wictextureloader.h"
#include "ddstextureloader.h"

#include "DevEnv/Internal/DirectXTex/D3DStats.h"
#include "KGCommon/Publish/Include/KG_FrameStats.h"

RC_NAMESPACE_BEGIN

RCRenderer_DX11::_DDSLoadInfoDX11 RCRenderer_DX11::m_DDSPool[RCRenderer_DX11::k_DDSPoolSize];

DXGI_FORMAT getPlatformFormat(RCGPUDATAFORMAT i_Format)
{
    DXGI_FORMAT l_TexFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    switch(i_Format)
    {
    case RGDF_RGBA8:
        l_TexFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
        break;

    case RGDF_BGRA8:
        l_TexFormat = DXGI_FORMAT_B8G8R8A8_UNORM;
        break;

    case RGDF_RGBA16F:
        l_TexFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
        break;

    case RGDF_RG32F:
        l_TexFormat = DXGI_FORMAT_R32G32_FLOAT;
        break;

	case RGDF_RGBA32F:
        l_TexFormat = DXGI_FORMAT_R32G32B32A32_FLOAT;
        break;

    case RGDF_D24S8:
        l_TexFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
        break;

    case RGDF_D32F:
        l_TexFormat = DXGI_FORMAT_D32_FLOAT;
        break;

    case RGDF_R32F:
        l_TexFormat = DXGI_FORMAT_R32_FLOAT;
        break;

    default:
        {
            DOME_ASSERT(0);
        }
    }
    return l_TexFormat;
}

RCGPUDATAFORMAT getTwineFormat(DXGI_FORMAT i_Format)
{
    RCGPUDATAFORMAT l_TexFormat = RGDF_RGBA8;
    if(i_Format == DXGI_FORMAT_R8G8B8A8_UNORM || i_Format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB || i_Format == DXGI_FORMAT_R8G8B8A8_TYPELESS)
        l_TexFormat = RGDF_RGBA8;
    else if(i_Format == DXGI_FORMAT_B8G8R8A8_UNORM || i_Format == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB || i_Format == DXGI_FORMAT_B8G8R8A8_TYPELESS)
        l_TexFormat = RGDF_BGRA8;
    else if(i_Format == DXGI_FORMAT_R16G16B16A16_FLOAT 
        || i_Format == DXGI_FORMAT_R16G16B16A16_UNORM
        || i_Format == DXGI_FORMAT_R16G16B16A16_TYPELESS)
        l_TexFormat = RGDF_RGBA16F;
    else if (i_Format == DXGI_FORMAT_R32G32_FLOAT
        || i_Format == DXGI_FORMAT_R32G32_TYPELESS)
        l_TexFormat = RGDF_RG32F;
	else if (i_Format == DXGI_FORMAT_R32G32B32A32_FLOAT 
        || i_Format == DXGI_FORMAT_R32G32B32A32_TYPELESS)
		l_TexFormat = RGDF_RGBA32F;
    else if(i_Format == DXGI_FORMAT_D24_UNORM_S8_UINT || i_Format == DXGI_FORMAT_R24G8_TYPELESS)
        l_TexFormat = RGDF_D24S8;
    else if(i_Format == DXGI_FORMAT_D32_FLOAT)
        l_TexFormat = RGDF_D32F;
    else if(i_Format == DXGI_FORMAT_R32_FLOAT)
        l_TexFormat = RGDF_R32F;
    else
    {
        l_TexFormat = RGDF_UNKNOWN;
    }
    return l_TexFormat;
}

Int getTwineFormatSize(RCGPUDATAFORMAT i_Format)
{
    switch (i_Format)
    {
    case RGDF_RGBA8:
    case RGDF_BGRA8:
    case RGDF_D24S8:
    case RGDF_D32F:
    case RGDF_R32F:
        return 4;
        break;

    case RGDF_RGBA16F:
    case RGDF_RG32F:
        return 8;
        break;

	case RGDF_RGBA32F:
        return 16;
        break;

    default:
        return 4;
        break;
    }
}

Int CalculateTextureSize(Int i_Width, Int i_Height, Int i_Depth, DXGI_FORMAT i_Format, Int i_Mipmaps, Int i_ArrayCount)
{
    Int l_BitPerPixel = 0;
    switch (i_Format)
    {
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
    case DXGI_FORMAT_R8G8B8A8_TYPELESS:
    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
    case DXGI_FORMAT_B8G8R8A8_TYPELESS:
    case DXGI_FORMAT_D24_UNORM_S8_UINT:
    case DXGI_FORMAT_R24G8_TYPELESS:
    case DXGI_FORMAT_D32_FLOAT:
        l_BitPerPixel = 32;
        break;

    case DXGI_FORMAT_R16G16B16A16_FLOAT:
    case DXGI_FORMAT_R16G16B16A16_UNORM:
    case DXGI_FORMAT_R16G16B16A16_TYPELESS:
        l_BitPerPixel = 64;
        break;
    case DXGI_FORMAT_R8_TYPELESS:
    case DXGI_FORMAT_R8_UNORM:
    case DXGI_FORMAT_R8_UINT:
    case DXGI_FORMAT_R8_SNORM:
    case DXGI_FORMAT_R8_SINT:
    case DXGI_FORMAT_A8_UNORM:
        l_BitPerPixel = 8;
        break;
    case DXGI_FORMAT_BC1_TYPELESS:
    case DXGI_FORMAT_BC1_UNORM:
    case DXGI_FORMAT_BC1_UNORM_SRGB:
        l_BitPerPixel = 4;
        break;
    case DXGI_FORMAT_BC2_TYPELESS:
    case DXGI_FORMAT_BC2_UNORM:
    case DXGI_FORMAT_BC2_UNORM_SRGB:
    case DXGI_FORMAT_BC3_TYPELESS:
    case DXGI_FORMAT_BC3_UNORM:
    case DXGI_FORMAT_BC3_UNORM_SRGB:
    case DXGI_FORMAT_BC4_TYPELESS:
    case DXGI_FORMAT_BC4_UNORM:
    case DXGI_FORMAT_BC4_SNORM:
    case DXGI_FORMAT_BC5_TYPELESS:
    case DXGI_FORMAT_BC5_UNORM:
    case DXGI_FORMAT_BC5_SNORM:
        l_BitPerPixel = 8;
        break;
    default:
        l_BitPerPixel = 32;
    }

    return i_Width * i_Height * i_Depth * l_BitPerPixel / 8;
}

// Texture related functions
DResult             RCRenderer_DX11::createTexture2D(OSTexture2D& o_Result, Int i_Width, Int i_Height, Int i_Mipmap, RCGPUDATAFORMAT i_Format, RCBUFFUSAGE i_Usage, Bool i_bTemp, void* i_pInitData, int i_WriteFlag)
{
    if(m_FreeTextureRes <= 0)
        return R_OUTOFRANGE;

    for (Int i = 0; i < k_MaxTextureRes; ++i)
    {
        if (m_TextureResPool[i].m_bFree)
        {
            if (i_bTemp)
            {
                m_TextureResPool[i].m_TempTexID = m_WorkSurfacePool.getWorkingSurface(i_Width, i_Height, i_Mipmap, i_Format, i_Usage, m_TextureResPool[i].m_TextureData, i_WriteFlag);
                if (m_TextureResPool[i].m_TempTexID < 0)
                {
                    return R_FAILED;
                }

                m_TextureResPool[i].m_bFree = DM_FALSE;
                m_TextureResPool[i].m_Width = i_Width;
                m_TextureResPool[i].m_Height = i_Height;
                m_TextureResPool[i].m_Mipmap = i_Mipmap;
                m_TextureResPool[i].m_Format = i_Format;
                m_TextureResPool[i].m_Usage = i_Usage;
                m_TextureResPool[i].m_TexCat = TEXCAT_TEMP;
                m_TextureResPool[i].m_VideoMemSize = CalculateTextureSize(i_Width, i_Height, 1, getPlatformFormat(i_Format), i_Mipmap, 1);
                m_CurTextureFromPool += m_TextureResPool[i].m_VideoMemSize;
                if (m_CurTextureFromPool > m_TextureFromPool)
                    m_TextureFromPool = m_CurTextureFromPool;

                o_Result.set(i, this);
                m_FreeTextureRes --;
                return R_SUCCESS;
            }
            else
            {
                DResult l_Result;
                l_Result = DX11CreateTexture2D(m_TextureResPool[i].m_TextureData, m_OSRendererData.m_pDevice, i_Width, i_Height, i_Mipmap, i_Format, i_Usage, i_pInitData);
                if (DM_FAIL(l_Result))
                {
                    return R_FAILED;
                }

                m_TextureResPool[i].m_bFree = DM_FALSE;
                m_TextureResPool[i].m_Width = i_Width;
                m_TextureResPool[i].m_Height = i_Height;
                m_TextureResPool[i].m_Mipmap = i_Mipmap;
                m_TextureResPool[i].m_Format = i_Format;
                m_TextureResPool[i].m_Usage = i_Usage;
                m_TextureResPool[i].m_TexCat = TEXCAT_NORMAL;
                m_TextureResPool[i].m_TempTexID = -1;
                m_TextureResPool[i].m_VideoMemSize = CalculateTextureSize(i_Width, i_Height, 1, getPlatformFormat(i_Format), i_Mipmap, 1);
                m_CurTextureCreated += m_TextureResPool[i].m_VideoMemSize;
                if (m_CurTextureCreated > m_TextureCreated)
                    m_TextureCreated = m_CurTextureCreated;

                o_Result.set(i, this);
                m_FreeTextureRes --;
                return R_SUCCESS;
            }
        }
    }
    return R_FAILED;
}

DResult             RCRenderer_DX11::generateMipmapsFromTexture2D(OSTexture2D& o_Result, OSTexture2D i_Tex)
{
    DResult l_Result;
    Int l_Width, l_Height, l_MaxSize;
    Int l_Mipmaps;
    RCGPUDATAFORMAT l_Format;

    l_Result = getTexture2DSize(i_Tex, l_Width, l_Height);
    DOME_ASSERT(DM_SUCC(l_Result));

    l_MaxSize = Math::Max(l_Width, l_Height);
    l_Mipmaps = Int(Math::Log2((F32)l_MaxSize)) + 1;

    l_Format = getTexture2DFormat(i_Tex);

    l_Result = createTexture2D(o_Result, l_Width, l_Height, -l_Mipmaps, l_Format, RBU_DEFAULT, DM_TRUE, NULL);
    DOME_ASSERT(DM_SUCC(l_Result));

    l_Result = copyTexture2DMipmap(o_Result, 0, i_Tex, 0);
    DOME_ASSERT(DM_SUCC(l_Result));

    m_OSRendererData.m_pDeviceContext->GenerateMips(m_TextureResPool[o_Result.getHandle()].m_TextureData.m_pShaderResourceView);

    return R_SUCCESS;
}

DResult             RCRenderer_DX11::createTexture2DFromFile(OSTexture2D& o_Result, const DString& i_TexFile)
{
    if(m_FreeTextureRes <= 0)
        return R_OUTOFRANGE;

    for (Int i = 0; i < k_MaxTextureRes; ++i)
    {
        if (m_TextureResPool[i].m_bFree)
        {
            DResult l_Result;
            l_Result = DX11LoadTexture2D(m_OSRendererData.m_pDevice, i_TexFile, m_TextureResPool[i].m_TextureData, 
                m_TextureResPool[i].m_Width, 
                m_TextureResPool[i].m_Height,
                m_TextureResPool[i].m_Mipmap, 
                m_TextureResPool[i].m_Format,
                m_TextureResPool[i].m_VideoMemSize);
            if (DM_FAIL(l_Result))
            {
                return R_FAILED;
            }

            m_TextureResPool[i].m_bFree = DM_FALSE;
            m_TextureResPool[i].m_Usage = RBU_IMMUTABLE;
            m_TextureResPool[i].m_TexCat = TEXCAT_LOADFROMFILE;
            m_TextureResPool[i].m_TempTexID = -1;

            m_CurTextureLoaded += m_TextureResPool[i].m_VideoMemSize;
            if (m_CurTextureLoaded > m_TextureLoaded)
                m_TextureLoaded = m_CurTextureLoaded;

            o_Result.set(i, this);
            m_FreeTextureRes --;
            return R_SUCCESS;
        }
    }
    return R_FAILED;
}

void                RCRenderer_DX11::refreshLoaded2DTexture(OSTexture2D i_Tex)
{
    Int l_Index = i_Tex.getHandle();
    if(l_Index < 0 || l_Index >= k_MaxTextureRes)
        return ;
    if(m_TextureResPool[l_Index].m_bFree)
        return ;
    if(m_TextureResPool[l_Index].m_TexCat != TEXCAT_LOADFROMFILE)
        return ;
    if(!m_pShaderCache)
        return ;
    if(!m_TextureResPool[l_Index].m_TextureData.m_pEngineTextureData)
        return ;
    ID3D11Resource* l_pTexture = NULL;
    ID3D11ShaderResourceView* l_pSRV = NULL;
    m_pShaderCache->refreshLoadedTexture(m_TextureResPool[l_Index].m_TextureData.m_pEngineTextureData,
        &l_pTexture, 
        &l_pSRV);
    m_TextureResPool[l_Index].m_TextureData.m_pTexture = (ID3D11Texture2D*)l_pTexture;
    m_TextureResPool[l_Index].m_TextureData.m_pShaderResourceView = l_pSRV;
}

DResult             RCRenderer_DX11::createTexture2DExternal(OSTexture2D& o_Result, const void* i_Param)
{
    RCOSTextureData* l_pTextureData = (RCOSTextureData*)i_Param;
    if(!l_pTextureData->m_pTexture)
        return R_FAILED;

    D3D11_TEXTURE2D_DESC l_TexDesc;
    ((ID3D11Texture2D*)l_pTextureData->m_pTexture)->GetDesc(&l_TexDesc);

    if(m_FreeTextureRes <= 0)
        return R_OUTOFRANGE;

    for (Int i = 0; i < k_MaxTextureRes; ++i)
    {
        if (m_TextureResPool[i].m_bFree)
        {
            m_TextureResPool[i].m_bFree = DM_FALSE;
            m_TextureResPool[i].m_Width = l_TexDesc.Width;
            m_TextureResPool[i].m_Height = l_TexDesc.Height;
            m_TextureResPool[i].m_Mipmap = l_TexDesc.MipLevels;
            m_TextureResPool[i].m_Format = getTwineFormat(l_TexDesc.Format);
            m_TextureResPool[i].m_Usage = RBU_IMMUTABLE;
            m_TextureResPool[i].m_TexCat = TEXCAT_EXTERNAL;
            m_TextureResPool[i].m_TempTexID = -1;
            m_TextureResPool[i].m_VideoMemSize = CalculateTextureSize(l_TexDesc.Width, l_TexDesc.Height, 1, l_TexDesc.Format, l_TexDesc.MipLevels, 1);

            m_CurTextureImported += m_TextureResPool[i].m_VideoMemSize;
            if (m_CurTextureImported > m_TextureImported)
                m_TextureImported = m_CurTextureImported;


            m_TextureResPool[i].m_TextureData = *l_pTextureData;

            o_Result.set(i, this);
            m_FreeTextureRes --;
            return R_SUCCESS;
        }
    }
    return R_FAILED;
}

DResult             RCRenderer_DX11::destroyTexture2D(OSTexture2D i_Tex)
{
    Int l_Index = i_Tex.getHandle();
    if(l_Index < 0 || l_Index >= k_MaxTextureRes)
        return R_OUTOFRANGE;
    if(m_TextureResPool[l_Index].m_bFree)
        return R_FAILED;

    switch (m_TextureResPool[l_Index].m_TexCat)
    {
    case TEXCAT_NORMAL:
        {
            m_TextureResPool[l_Index].m_bFree = DM_TRUE;
            m_FreeTextureRes ++;
            m_TextureResPool[l_Index].m_Width = 0;
            m_TextureResPool[l_Index].m_Height = 0;
            m_TextureResPool[l_Index].m_Mipmap = 0;
            m_TextureResPool[l_Index].m_Format = RGDF_UNKNOWN;
            m_TextureResPool[l_Index].m_TexCat = TEXCAT_NORMAL;
            m_TextureResPool[l_Index].m_Usage = RBU_DEFAULT;
            m_TextureResPool[l_Index].m_TempTexID = -1;

             m_CurTextureCreated -= m_TextureResPool[l_Index].m_VideoMemSize;
            if (m_CurTextureCreated > m_TextureCreated)
                m_TextureCreated = m_CurTextureCreated;

            m_TextureResPool[l_Index].m_VideoMemSize = 0;
            RC_RELEASE(m_TextureResPool[l_Index].m_TextureData.m_pRenderTargetView);
            RC_RELEASE(m_TextureResPool[l_Index].m_TextureData.m_pShaderResourceView);
			RC_RELEASE(m_TextureResPool[l_Index].m_TextureData.m_pDepthStencilView);
            RC_RELEASE(m_TextureResPool[l_Index].m_TextureData.m_pTexture);
        }
        break;

    case TEXCAT_TEMP:
        {
            m_TextureResPool[l_Index].m_bFree = DM_TRUE;
            m_FreeTextureRes ++;
            m_TextureResPool[l_Index].m_Width = 0;
            m_TextureResPool[l_Index].m_Height = 0;
            m_TextureResPool[l_Index].m_Mipmap = 0;
            m_TextureResPool[l_Index].m_Format = RGDF_UNKNOWN;
            m_TextureResPool[l_Index].m_TexCat = TEXCAT_NORMAL;
            m_TextureResPool[l_Index].m_Usage = RBU_DEFAULT;
            m_WorkSurfacePool.doneWithWorkingSurface(m_TextureResPool[l_Index].m_TempTexID);
            m_TextureResPool[l_Index].m_TempTexID = -1;

            m_CurTextureFromPool -= m_TextureResPool[l_Index].m_VideoMemSize;
            if (m_CurTextureFromPool > m_TextureFromPool)
                m_TextureFromPool = m_CurTextureFromPool;

            m_TextureResPool[l_Index].m_VideoMemSize = 0;
            m_TextureResPool[l_Index].m_TextureData.m_pRenderTargetView = DM_NULL;
            m_TextureResPool[l_Index].m_TextureData.m_pShaderResourceView = DM_NULL;
			m_TextureResPool[l_Index].m_TextureData.m_pDepthStencilView = DM_NULL;
            m_TextureResPool[l_Index].m_TextureData.m_pTexture = DM_NULL;
        }
        break;

    case TEXCAT_LOADFROMFILE:
        {
            m_TextureResPool[l_Index].m_bFree = DM_TRUE;
            m_FreeTextureRes ++;
            m_TextureResPool[l_Index].m_Width = 0;
            m_TextureResPool[l_Index].m_Height = 0;
            m_TextureResPool[l_Index].m_Mipmap = 0;
            m_TextureResPool[l_Index].m_Format = RGDF_UNKNOWN;
            m_TextureResPool[l_Index].m_TexCat = TEXCAT_NORMAL;
            m_TextureResPool[l_Index].m_Usage = RBU_DEFAULT;
            m_TextureResPool[l_Index].m_TempTexID = -1;

            m_CurTextureLoaded -= m_TextureResPool[l_Index].m_VideoMemSize;
            if (m_CurTextureLoaded > m_TextureLoaded)
                m_TextureLoaded = m_CurTextureLoaded;

            m_TextureResPool[l_Index].m_VideoMemSize = 0;
            if (m_pShaderCache)
            {
                m_pShaderCache->unloadTextureFile(m_TextureResPool[l_Index].m_TextureData.m_pEngineTextureData);
                m_TextureResPool[l_Index].m_TextureData.m_pEngineTextureData = NULL;
                m_TextureResPool[l_Index].m_TextureData.m_pRenderTargetView = NULL;
                m_TextureResPool[l_Index].m_TextureData.m_pShaderResourceView = NULL;
                m_TextureResPool[l_Index].m_TextureData.m_pDepthStencilView = NULL;
                m_TextureResPool[l_Index].m_TextureData.m_pTexture = NULL;
            }
            else
            {
                RC_RELEASE(m_TextureResPool[l_Index].m_TextureData.m_pRenderTargetView);
                RC_RELEASE(m_TextureResPool[l_Index].m_TextureData.m_pShaderResourceView);
                RC_RELEASE(m_TextureResPool[l_Index].m_TextureData.m_pDepthStencilView);
                RC_RELEASE(m_TextureResPool[l_Index].m_TextureData.m_pTexture);
            }
        }
        break;

    case TEXCAT_EXTERNAL:
        {
            m_TextureResPool[l_Index].m_bFree = DM_TRUE;
            m_FreeTextureRes ++;
            m_TextureResPool[l_Index].m_Width = 0;
            m_TextureResPool[l_Index].m_Height = 0;
            m_TextureResPool[l_Index].m_Mipmap = 0;
            m_TextureResPool[l_Index].m_Format = RGDF_UNKNOWN;
            m_TextureResPool[l_Index].m_TexCat = TEXCAT_NORMAL;
            m_TextureResPool[l_Index].m_Usage = RBU_DEFAULT;
            m_TextureResPool[l_Index].m_TempTexID = -1;

            m_CurTextureImported -= m_TextureResPool[l_Index].m_VideoMemSize;
            if (m_CurTextureImported > m_TextureImported)
                m_TextureImported = m_CurTextureImported;

            m_TextureResPool[l_Index].m_VideoMemSize = 0;
            m_TextureResPool[l_Index].m_TextureData.m_pRenderTargetView = DM_NULL;
            m_TextureResPool[l_Index].m_TextureData.m_pShaderResourceView = DM_NULL;
			m_TextureResPool[l_Index].m_TextureData.m_pDepthStencilView = DM_NULL;
            m_TextureResPool[l_Index].m_TextureData.m_pTexture = DM_NULL;
        }
        break;
    }
    return R_SUCCESS;
}

DResult             RCRenderer_DX11::clearTexturePool()
{
    m_WorkSurfacePool.reset();
    return R_SUCCESS;
}

RCBUFFUSAGE         RCRenderer_DX11::getTexture2DUsage(OSTexture2D i_Tex)
{
    Int l_Index = i_Tex.getHandle();
    DOME_ASSERT(l_Index >= 0 || l_Index < k_MaxTextureRes);
    DOME_ASSERT(!m_TextureResPool[l_Index].m_bFree);

    return m_TextureResPool[l_Index].m_Usage;
}

RCGPUDATAFORMAT     RCRenderer_DX11::getTexture2DFormat(OSTexture2D i_Tex)
{
    Int l_Index = i_Tex.getHandle();
    DOME_ASSERT(l_Index >= 0 || l_Index < k_MaxTextureRes);
    DOME_ASSERT(!m_TextureResPool[l_Index].m_bFree);

    return m_TextureResPool[l_Index].m_Format;
}

DResult             RCRenderer_DX11::getTexture2DSize(OSTexture2D i_Tex, Int& o_Width, Int& o_Height)
{
    Int l_Index = i_Tex.getHandle();
    if(l_Index < 0 || l_Index >= k_MaxTextureRes)
        return R_OUTOFRANGE;
    if(m_TextureResPool[l_Index].m_bFree)
        return R_FAILED;

    o_Width = m_TextureResPool[l_Index].m_Width;
    o_Height = m_TextureResPool[l_Index].m_Height;
    return R_SUCCESS;
}

DResult             RCRenderer_DX11::getTexture2DSize(OSTexture2D i_Tex, Int i_Mipmap, Int& o_Width, Int& o_Height)
{
    Int l_Index = i_Tex.getHandle();
    if(l_Index < 0 || l_Index >= k_MaxTextureRes)
        return R_OUTOFRANGE;
    if(m_TextureResPool[l_Index].m_bFree)
        return R_FAILED;

    Int l_Width = m_TextureResPool[l_Index].m_Width;
    Int l_Height = m_TextureResPool[l_Index].m_Height;

    if (i_Mipmap < 0)
    {
        o_Width = 1;
        o_Height = 1;
    }
    else
    {
        for (Int i = 0; i < i_Mipmap; ++i)
        {
            l_Width /= 2;
            l_Height /= 2;
        }
        if(l_Width == 0)
            l_Width = 1;
        if(l_Height == 0)
            l_Height = 1;

        o_Width = l_Width;
        o_Height = l_Height;
    }

    return R_SUCCESS;
}

DResult             RCRenderer_DX11::getTexture2DMipmaps(OSTexture2D i_Tex, Int& o_Mipmap)
{
    Int l_Index = i_Tex.getHandle();
    if(l_Index < 0 || l_Index >= k_MaxTextureRes)
        return R_OUTOFRANGE;
    if(m_TextureResPool[l_Index].m_bFree)
        return R_FAILED;

    o_Mipmap = m_TextureResPool[l_Index].m_Mipmap;
    return R_SUCCESS;
}

Bool                RCRenderer_DX11::isTexture2DTemp(OSTexture2D i_Tex)
{
    Int l_Index = i_Tex.getHandle();
    DOME_ASSERT(l_Index >= 0 || l_Index < k_MaxTextureRes);
    DOME_ASSERT(!m_TextureResPool[l_Index].m_bFree);

    return m_TextureResPool[l_Index].m_TexCat == TEXCAT_TEMP;
}

Bool                RCRenderer_DX11::isTexture2DRT(OSTexture2D i_Tex)
{
    Int l_Index = i_Tex.getHandle();
    DOME_ASSERT(l_Index >= 0 || l_Index < k_MaxTextureRes);
    DOME_ASSERT(!m_TextureResPool[l_Index].m_bFree);

    return m_TextureResPool[l_Index].m_TextureData.m_pRenderTargetView != DM_NULL;
}

DResult             RCRenderer_DX11::lockTexture2D(OSTexture2D i_Tex, Int i_Mipmap, RCBUFFLOCKSTYLE i_LockStyle, RCTexLockedRect& o_LockResult)
{
    if (getTexture2DUsage(i_Tex) == RBU_STAGE || getTexture2DUsage(i_Tex) == RBU_DYNAMIC)
    {
        D3D11_MAP l_map = D3D11_MAP_READ_WRITE;
        if(i_LockStyle == RTLS_READONLY)
            l_map = D3D11_MAP_READ;
        else if(i_LockStyle == RTLS_WRITEONLY)
            l_map = D3D11_MAP_WRITE_DISCARD;
        else if(i_LockStyle == RTLS_READWRITE)
            l_map = D3D11_MAP_READ_WRITE;
        D3D11_MAPPED_SUBRESOURCE l_mapss;
        HRESULT hr = DS_Map(m_OSRendererData.m_pDeviceContext, m_TextureResPool[i_Tex.getHandle()].m_TextureData.m_pTexture, i_Mipmap, l_map, 0, &l_mapss);
        if(hr != S_OK)
        {
            return R_FAILED;
        }

        o_LockResult.m_pBits = l_mapss.pData;
        o_LockResult.m_Pitch = l_mapss.RowPitch;
        return R_SUCCESS;
    }

    return R_FAILED;
}

DResult             RCRenderer_DX11::unlockTexture2D(OSTexture2D i_Tex, Int i_Mipmap)
{
    if (getTexture2DUsage(i_Tex) == RBU_STAGE || getTexture2DUsage(i_Tex) == RBU_DYNAMIC)
    {
        DS_Unmap(m_OSRendererData.m_pDeviceContext, m_TextureResPool[i_Tex.getHandle()].m_TextureData.m_pTexture, i_Mipmap);
        return R_SUCCESS;
    }
    return R_FAILED;
}

DResult             RCRenderer_DX11::copyTexture2D(OSTexture2D i_DestTex, OSTexture2D i_SrcTex)
{
    Int l_DstIndex = i_DestTex.getHandle();
    DOME_ASSERT(l_DstIndex >= 0 || l_DstIndex < k_MaxTextureRes);
    DOME_ASSERT(!m_TextureResPool[l_DstIndex].m_bFree);
    Int l_SrcIndex = i_SrcTex.getHandle();
    DOME_ASSERT(l_SrcIndex >= 0 || l_SrcIndex < k_MaxTextureRes);
    DOME_ASSERT(!m_TextureResPool[l_SrcIndex].m_bFree);

    ID3D11Texture2D* l_pSrcTex = m_TextureResPool[l_SrcIndex].m_TextureData.m_pTexture;
    ID3D11Texture2D* l_pDstTex = m_TextureResPool[l_DstIndex].m_TextureData.m_pTexture;

    m_OSRendererData.m_pDeviceContext->CopyResource(l_pDstTex, l_pSrcTex);
    return R_SUCCESS;
}

DResult             RCRenderer_DX11::copyTexture2DMipmap(OSTexture2D i_DestTex, Int i_DestMipmap, OSTexture2D i_SrcTex, Int i_SrcMipmap)
{
    Int l_DstIndex = i_DestTex.getHandle();
    DOME_ASSERT(l_DstIndex >= 0 || l_DstIndex < k_MaxTextureRes);
    DOME_ASSERT(!m_TextureResPool[l_DstIndex].m_bFree);
    Int l_SrcIndex = i_SrcTex.getHandle();
    DOME_ASSERT(l_SrcIndex >= 0 || l_SrcIndex < k_MaxTextureRes);
    DOME_ASSERT(!m_TextureResPool[l_SrcIndex].m_bFree);

    ID3D11Texture2D* l_pSrcTex = m_TextureResPool[l_SrcIndex].m_TextureData.m_pTexture;
    ID3D11Texture2D* l_pDstTex = m_TextureResPool[l_DstIndex].m_TextureData.m_pTexture;

    m_OSRendererData.m_pDeviceContext->CopySubresourceRegion(l_pDstTex, i_DestMipmap, 0, 0, 0, l_pSrcTex, i_SrcMipmap, DM_NULL);
    return R_SUCCESS;
}

DResult              RCRenderer_DX11::createTexture3DFromFile(OSTexture3D& o_Result, const DString& i_TexFile)
{
    if (m_FreeTexture3DRes <= 0)
        return R_OUTOFRANGE;

    for (Int i = 0; i < k_MaxTexture3DRes; ++i)
    {
        if (m_Texture3DResPool[i].m_bFree)
        {
            DResult l_Result;
            l_Result = DX11LoadTexture3D(m_OSRendererData.m_pDevice, i_TexFile, m_Texture3DResPool[i].m_TextureData,
                m_Texture3DResPool[i].m_Width,
                m_Texture3DResPool[i].m_Height,
                m_Texture3DResPool[i].m_Depth,
                m_Texture3DResPool[i].m_Mipmap,
                m_Texture3DResPool[i].m_Format, 
                m_Texture3DResPool[i].m_VideoMemSize);
            if (DM_FAIL(l_Result))
            {
                return R_FAILED;
            }

            m_Texture3DResPool[i].m_bFree = DM_FALSE;
            m_Texture3DResPool[i].m_Usage = RBU_IMMUTABLE;
            m_Texture3DResPool[i].m_TexCat = TEXCAT_LOADFROMFILE;
            m_Texture3DResPool[i].m_TempTexID = -1;

            m_CurTextureLoaded += m_Texture3DResPool[i].m_VideoMemSize;
            if (m_CurTextureLoaded > m_TextureLoaded)
                m_TextureLoaded = m_CurTextureLoaded;

            o_Result.set(i, this);
            m_FreeTexture3DRes--;
            return R_SUCCESS;
        }
    }
    return R_FAILED;
}

DResult              RCRenderer_DX11::destroyTexture3D(OSTexture3D i_Tex)
{
    Int l_Index = i_Tex.getHandle();
    if (l_Index < 0 || l_Index >= k_MaxTexture3DRes)
        return R_OUTOFRANGE;
    if (m_Texture3DResPool[l_Index].m_bFree)
        return R_FAILED;

    switch (m_Texture3DResPool[l_Index].m_TexCat)
    {
    case TEXCAT_LOADFROMFILE:
        {
            m_Texture3DResPool[l_Index].m_bFree = DM_TRUE;
            m_FreeTexture3DRes++;
            m_Texture3DResPool[l_Index].m_Width = 0;
            m_Texture3DResPool[l_Index].m_Height = 0;
            m_Texture3DResPool[l_Index].m_Mipmap = 0;
            m_Texture3DResPool[l_Index].m_Format = RGDF_UNKNOWN;
            m_Texture3DResPool[l_Index].m_TexCat = TEXCAT_NORMAL;
            m_Texture3DResPool[l_Index].m_Usage = RBU_DEFAULT;
            m_Texture3DResPool[l_Index].m_TempTexID = -1;

            m_CurTextureLoaded -= m_Texture3DResPool[l_Index].m_VideoMemSize;
            if (m_CurTextureLoaded > m_TextureLoaded)
                m_TextureLoaded = m_CurTextureLoaded;

            m_Texture3DResPool[l_Index].m_VideoMemSize = 0;
            UnloadDDSTexture(m_Texture3DResPool[l_Index].m_TextureData.m_pEngineTextureData);
            m_Texture3DResPool[l_Index].m_TextureData.m_pDepthStencilView = DM_NULL;
            m_Texture3DResPool[l_Index].m_TextureData.m_pEngineTextureData = DM_NULL;
            m_Texture3DResPool[l_Index].m_TextureData.m_pRenderTargetView = DM_NULL;
            m_Texture3DResPool[l_Index].m_TextureData.m_pShaderResourceView = DM_NULL;
            m_Texture3DResPool[l_Index].m_TextureData.m_pTexture = DM_NULL;
            m_Texture3DResPool[l_Index].m_TextureData.m_pUAV = DM_NULL;
        }
        break;

    default:
        {
            DOME_ASSERT(0);
        }
    }
    return R_SUCCESS;
}

DResult              RCRenderer_DX11::createTextureCubeFromFile(OSTextureCube& o_Result, const DString& i_TexFile)
{
    if (m_FreeTextureCubeRes <= 0)
        return R_OUTOFRANGE;

    for (Int i = 0; i < k_MaxTextureCubeRes; ++i)
    {
        if (m_TextureCubeResPool[i].m_bFree)
        {
            DResult l_Result;
            l_Result = DX11LoadTextureCube(m_OSRendererData.m_pDevice, i_TexFile, m_TextureCubeResPool[i].m_TextureData,
                m_TextureCubeResPool[i].m_Width,
                m_TextureCubeResPool[i].m_Height,
                m_TextureCubeResPool[i].m_Depth,
                m_TextureCubeResPool[i].m_Mipmap,
                m_TextureCubeResPool[i].m_Format,
                m_TextureCubeResPool[i].m_VideoMemSize);
            if (DM_FAIL(l_Result))
            {
                return R_FAILED;
            }

            m_TextureCubeResPool[i].m_bFree = DM_FALSE;
            m_TextureCubeResPool[i].m_Usage = RBU_IMMUTABLE;
            m_TextureCubeResPool[i].m_TexCat = TEXCAT_LOADFROMFILE;
            m_TextureCubeResPool[i].m_TempTexID = -1;

            m_CurTextureLoaded += m_TextureCubeResPool[i].m_VideoMemSize;
            if (m_CurTextureLoaded > m_TextureLoaded)
                m_TextureLoaded = m_CurTextureLoaded;

            o_Result.set(i, this);
            m_FreeTextureCubeRes--;
            return R_SUCCESS;
        }
    }
    return R_FAILED;
}

DResult              RCRenderer_DX11::destroyTextureCube(OSTextureCube i_Tex)
{
    Int l_Index = i_Tex.getHandle();
    if (l_Index < 0 || l_Index >= k_MaxTextureCubeRes)
        return R_OUTOFRANGE;
    if (m_TextureCubeResPool[l_Index].m_bFree)
        return R_FAILED;

    switch (m_TextureCubeResPool[l_Index].m_TexCat)
    {
    case TEXCAT_LOADFROMFILE:
    {
        m_TextureCubeResPool[l_Index].m_bFree = DM_TRUE;
        m_FreeTextureCubeRes++;
        m_TextureCubeResPool[l_Index].m_Width = 0;
        m_TextureCubeResPool[l_Index].m_Height = 0;
        m_TextureCubeResPool[l_Index].m_Mipmap = 0;
        m_TextureCubeResPool[l_Index].m_Format = RGDF_UNKNOWN;
        m_TextureCubeResPool[l_Index].m_TexCat = TEXCAT_NORMAL;
        m_TextureCubeResPool[l_Index].m_Usage = RBU_DEFAULT;
        m_TextureCubeResPool[l_Index].m_TempTexID = -1;

        m_CurTextureLoaded -= m_TextureCubeResPool[l_Index].m_VideoMemSize;
        if (m_CurTextureLoaded > m_TextureLoaded)
            m_TextureLoaded = m_CurTextureLoaded;

        m_TextureCubeResPool[l_Index].m_VideoMemSize = 0;
        UnloadDDSTexture(m_TextureCubeResPool[l_Index].m_TextureData.m_pEngineTextureData);
        m_TextureCubeResPool[l_Index].m_TextureData.m_pDepthStencilView = DM_NULL;
        m_TextureCubeResPool[l_Index].m_TextureData.m_pEngineTextureData = DM_NULL;
        m_TextureCubeResPool[l_Index].m_TextureData.m_pRenderTargetView = DM_NULL;
        m_TextureCubeResPool[l_Index].m_TextureData.m_pShaderResourceView = DM_NULL;
        m_TextureCubeResPool[l_Index].m_TextureData.m_pTexture = DM_NULL;
        m_TextureCubeResPool[l_Index].m_TextureData.m_pUAV = DM_NULL;
    }
    break;

    default:
    {
        DOME_ASSERT(0);
    }
    }
    return R_SUCCESS;
}

DResult              RCRenderer_DX11::DX11CreateTexture2D(RCOSTextureData& o_TexData, ID3D11Device* i_pD3D11Device, Int i_Width, Int i_Height, Int i_Mipmap, RCGPUDATAFORMAT i_Format, RCBUFFUSAGE i_Usage, void* i_pInitData)
{
    if(i_Format == RGDF_D24S8 || i_Format == RGDF_D32F)
        return DX11CreateDepthTexture2D(o_TexData, i_pD3D11Device, i_Width, i_Height, i_Format);

    HRESULT l_Hr;
    D3D11_TEXTURE2D_DESC l_Desc;

    o_TexData.m_pTexture = DM_NULL;
    o_TexData.m_pShaderResourceView = DM_NULL;
    o_TexData.m_pRenderTargetView = DM_NULL;

    if (i_Usage == RBU_DEFAULT)
    {
        l_Desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
        l_Desc.CPUAccessFlags = 0;
        l_Desc.Usage = D3D11_USAGE_DEFAULT;
    }
    else if (i_Usage == RBU_IMMUTABLE)
    {
        l_Desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        l_Desc.CPUAccessFlags = 0;
        l_Desc.Usage = D3D11_USAGE_IMMUTABLE;
    }
    else if (i_Usage == RBU_DYNAMIC)
    {
        l_Desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        l_Desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        l_Desc.Usage = D3D11_USAGE_DYNAMIC;
    }
    else
    {
        l_Desc.BindFlags = 0;
        l_Desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE|D3D11_CPU_ACCESS_READ;
        l_Desc.Usage = D3D11_USAGE_STAGING;
    }
    l_Desc.ArraySize = 1;
    l_Desc.Format = getPlatformFormat(i_Format);
    l_Desc.Width = i_Width;
    l_Desc.Height = i_Height;
    l_Desc.MipLevels = Math::Abs(i_Mipmap);
    l_Desc.MiscFlags = 0;
    if(i_Mipmap < 0)
        l_Desc.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;
    l_Desc.SampleDesc.Count = 1;
    l_Desc.SampleDesc.Quality = 0;

    D3D11_SUBRESOURCE_DATA l_InitData;
    l_InitData.pSysMem = i_pInitData;
    l_InitData.SysMemPitch = i_Width * getTwineFormatSize(i_Format);

    l_Hr = DS_CreateTexture2D(i_pD3D11Device, &l_Desc, i_pInitData ? &l_InitData : DM_NULL, &o_TexData.m_pTexture);
    DOME_ASSERT(SUCCEEDED(l_Hr));

    // create shader resource view
    if (i_Usage == RBU_DEFAULT ||
        i_Usage == RBU_IMMUTABLE ||
        i_Usage == RBU_DYNAMIC)
    {
        D3D11_SHADER_RESOURCE_VIEW_DESC l_srvDesc;
        l_srvDesc.Format = getPlatformFormat(i_Format);
        l_srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        l_srvDesc.Texture2D.MipLevels = Math::Abs(i_Mipmap);
        l_srvDesc.Texture2D.MostDetailedMip = 0;
        l_Hr = i_pD3D11Device->CreateShaderResourceView(o_TexData.m_pTexture, &l_srvDesc, &o_TexData.m_pShaderResourceView);
        DOME_ASSERT(SUCCEEDED(l_Hr));
    }

    // create render target view
    if (i_Usage == RBU_DEFAULT)
    {
        D3D11_RENDER_TARGET_VIEW_DESC l_rtvDesc;
        l_rtvDesc.Format = getPlatformFormat(i_Format);
        l_rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
        l_rtvDesc.Texture2D.MipSlice = 0;
        l_Hr = i_pD3D11Device->CreateRenderTargetView(o_TexData.m_pTexture, &l_rtvDesc, &o_TexData.m_pRenderTargetView);
        DOME_ASSERT(SUCCEEDED(l_Hr));
    }

    return R_SUCCESS;
}

DResult              RCRenderer_DX11::DX11CreateDepthTexture2D(RCOSTextureData& o_TexData, ID3D11Device* i_pD3D11Device, Int i_Width, Int i_Height, RCGPUDATAFORMAT i_Format)
{
    HRESULT l_Hr;
    D3D11_TEXTURE2D_DESC l_Desc;
    DXGI_FORMAT         l_TexFormat;
    DXGI_FORMAT         l_SrvFormat;
    DXGI_FORMAT         l_DsvFormat;

    if (i_Format == RGDF_D24S8)
    {
        l_TexFormat = DXGI_FORMAT_R24G8_TYPELESS;
        l_SrvFormat = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
        l_DsvFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;
    }
    else if (i_Format == RGDF_D32F)
    {
        l_TexFormat = DXGI_FORMAT_R32_FLOAT;
        l_SrvFormat = DXGI_FORMAT_R32_FLOAT;
        l_DsvFormat = DXGI_FORMAT_D32_FLOAT;
    }
    else
    {
        DOME_ASSERT(0);
    }

    o_TexData.m_pTexture = DM_NULL;
    o_TexData.m_pShaderResourceView = DM_NULL;
    o_TexData.m_pRenderTargetView = DM_NULL;

    l_Desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_DEPTH_STENCIL;
    l_Desc.CPUAccessFlags = 0;
    l_Desc.Usage = D3D11_USAGE_DEFAULT;

    l_Desc.ArraySize = 1;
    l_Desc.Format = l_TexFormat;
    l_Desc.Width = i_Width;
    l_Desc.Height = i_Height;
    l_Desc.MipLevels = 1;
    l_Desc.MiscFlags = 0;
    l_Desc.SampleDesc.Count = 1;
    l_Desc.SampleDesc.Quality = 0;

    l_Hr = DS_CreateTexture2D(i_pD3D11Device, &l_Desc, DM_NULL, &o_TexData.m_pTexture);
    DOME_ASSERT(SUCCEEDED(l_Hr));

    {
        D3D11_SHADER_RESOURCE_VIEW_DESC l_srvDesc;
        l_srvDesc.Format = l_SrvFormat;
        l_srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        l_srvDesc.Texture2D.MipLevels = 1;
        l_srvDesc.Texture2D.MostDetailedMip = 0;
        l_Hr = i_pD3D11Device->CreateShaderResourceView(o_TexData.m_pTexture, &l_srvDesc, &o_TexData.m_pShaderResourceView);
        DOME_ASSERT(SUCCEEDED(l_Hr));
    }

    // create depth stencil view
    {
        D3D11_DEPTH_STENCIL_VIEW_DESC l_dsvDesc;
        l_dsvDesc.Format = l_DsvFormat;
        l_dsvDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2DMS;
        l_dsvDesc.Flags = 0;
        l_Hr = i_pD3D11Device->CreateDepthStencilView(o_TexData.m_pTexture, &l_dsvDesc, &o_TexData.m_pDepthStencilView);
        DOME_ASSERT(SUCCEEDED(l_Hr));
    }

    return R_SUCCESS;

}

DResult              RCRenderer_DX11::DX11LoadTexture2D(ID3D11Device* i_pD3D11Device, const DString& i_FileName, RCOSTextureData& o_TexData, Int& o_Width, Int& o_Height, Int& o_Mipmap, RCGPUDATAFORMAT& o_Format, Int& o_VideoMemSize)
{
    if (m_pShaderCache)
    {
        ID3D11Resource* l_pTexture = NULL;
        ID3D11ShaderResourceView* l_pSRV = NULL;
        void* l_pEngineTextureData = NULL;

        l_pEngineTextureData = m_pShaderCache->loadTextureFromFile(i_FileName.c_str(), &l_pTexture, &l_pSRV);
        DOME_ASSERT(l_pEngineTextureData);
        if (!l_pEngineTextureData || !l_pTexture || !l_pSRV)
            return R_FAILED;

        D3D11_TEXTURE2D_DESC l_TexDesc;
        ((ID3D11Texture2D*)l_pTexture)->GetDesc(&l_TexDesc);
        o_Width = l_TexDesc.Width;
        o_Height = l_TexDesc.Height;
        o_Mipmap = l_TexDesc.MipLevels;
        o_Format = getTwineFormat(l_TexDesc.Format);

        o_TexData.m_pTexture = (ID3D11Texture2D*)l_pTexture;
        o_TexData.m_pShaderResourceView = l_pSRV;
        o_TexData.m_pRenderTargetView = DM_NULL;
        o_TexData.m_pEngineTextureData = l_pEngineTextureData;

        o_VideoMemSize = CalculateTextureSize(l_TexDesc.Width, l_TexDesc.Height, 1, l_TexDesc.Format, l_TexDesc.MipLevels, 1);

        return R_SUCCESS;

    }
    else
    {
        ID3D11Resource* l_pTexture;
        ID3D11ShaderResourceView* l_pSRV;
        HRESULT hr;

        DFile l_ImageFile(i_FileName.c_str(), DOME_GetExternalFS());
        hr = l_ImageFile.open(DM_FALSE);
        if (DM_FAIL(hr))
            return hr;

        Int l_ImageFileLen = l_ImageFile.getLength();
        Char* l_pImageBuffer = (Char*)DOME_Alloc(l_ImageFileLen);
        hr = l_ImageFile.read(l_pImageBuffer, l_ImageFileLen);
        l_ImageFile.close();
        DOME_ASSERT(DM_SUCC(hr));

        const char* l_pPostfix = i_FileName.c_str() + i_FileName.size() - 3;
        if (i_FileName.size() > 3 &&
            (l_pPostfix[0] == 'D' ||
                l_pPostfix[0] == 'd' ||
                l_pPostfix[1] == 'D' ||
                l_pPostfix[1] == 'd' ||
                l_pPostfix[2] == 'S' ||
                l_pPostfix[2] == 's')
            )
            hr = DirectX::CreateDDSTextureFromMemory(i_pD3D11Device, (uint8_t*)l_pImageBuffer, l_ImageFileLen, &l_pTexture, &l_pSRV);
        else
        {
            //DOME_WARNING2(0, "RC load texture: %s", i_FileName.c_str());
            hr = DirectX::CreateWICTextureFromMemory(i_pD3D11Device, (uint8_t*)l_pImageBuffer, l_ImageFileLen, &l_pTexture, &l_pSRV);
        }

        DOME_Free(l_pImageBuffer);
        if (hr != S_OK)
            return R_FAILED;

        D3D11_TEXTURE2D_DESC l_TexDesc;
        ((ID3D11Texture2D*)l_pTexture)->GetDesc(&l_TexDesc);
        o_Width = l_TexDesc.Width;
        o_Height = l_TexDesc.Height;
        o_Mipmap = l_TexDesc.MipLevels;
        o_Format = getTwineFormat(l_TexDesc.Format);

        o_TexData.m_pTexture = (ID3D11Texture2D*)l_pTexture;
        o_TexData.m_pShaderResourceView = l_pSRV;
        o_TexData.m_pRenderTargetView = DM_NULL;

        o_VideoMemSize = CalculateTextureSize(l_TexDesc.Width, l_TexDesc.Height, 1, l_TexDesc.Format, l_TexDesc.MipLevels, 1);

        return R_SUCCESS;
    }
}

DResult              RCRenderer_DX11::DX11LoadTexture3D(ID3D11Device* i_pD3D11Device, const DString& i_FileName, RCOSTextureData& o_TexData, Int& o_Width, Int& o_Height, Int& o_Depth, Int& o_Mipmap, RCGPUDATAFORMAT& o_Format, Int& o_VideoMemSize)
{
    ID3D11Resource* l_pTexture;
    ID3D11ShaderResourceView* l_pSRV;

    const char* l_pPostfix = i_FileName.c_str() + i_FileName.size() - 3;
    if (i_FileName.size() > 3 &&
        (l_pPostfix[0] == 'D' ||
            l_pPostfix[0] == 'd' ||
            l_pPostfix[1] == 'D' ||
            l_pPostfix[1] == 'd' ||
            l_pPostfix[2] == 'S' ||
            l_pPostfix[2] == 's')
        )
    {
        D3D11_RESOURCE_DIMENSION l_Dim;
        Int l_LayerCount;
        DXGI_FORMAT l_Format;
        void* pLoadTex = LoadDDSTexture(i_pD3D11Device, i_FileName, &l_pTexture, &l_pSRV, l_Dim, o_Width, o_Height, o_Depth, o_Mipmap, l_LayerCount, l_Format);
        if (pLoadTex)
        {
            if (l_Dim != D3D11_RESOURCE_DIMENSION_TEXTURE3D)
            {
                UnloadDDSTexture(pLoadTex);
                return R_FAILED;
            }
            else
            {
                o_TexData.m_pTexture = (ID3D11Texture2D*)l_pTexture;
                o_TexData.m_pShaderResourceView = l_pSRV;
                o_TexData.m_pRenderTargetView = DM_NULL;
                o_TexData.m_pEngineTextureData = pLoadTex;
                o_Format = getTwineFormat(l_Format);
                o_VideoMemSize = CalculateTextureSize(o_Width, o_Height, o_Depth, l_Format, o_Mipmap, 1);

                return R_SUCCESS;
            }
        }
        else
            return R_FAILED;
    }
    else
        return R_FAILED;
}

DResult              RCRenderer_DX11::DX11LoadTextureCube(ID3D11Device* i_pD3D11Device, const DString& i_FileName, RCOSTextureData& o_TexData, Int& o_Width, Int& o_Height, Int& o_Depth, Int& o_Mipmap, RCGPUDATAFORMAT& o_Format, Int& o_VideoMemSize)
{
    ID3D11Resource* l_pTexture;
    ID3D11ShaderResourceView* l_pSRV;

    const char* l_pPostfix = i_FileName.c_str() + i_FileName.size() - 3;
    if (i_FileName.size() > 3 &&
        (l_pPostfix[0] == 'D' ||
            l_pPostfix[0] == 'd' ||
            l_pPostfix[1] == 'D' ||
            l_pPostfix[1] == 'd' ||
            l_pPostfix[2] == 'S' ||
            l_pPostfix[2] == 's')
        )
    {
        D3D11_RESOURCE_DIMENSION l_Dim;
        Int l_LayerCount;
        DXGI_FORMAT l_Format;
        void* pLoadTex = LoadDDSTexture(i_pD3D11Device, i_FileName, &l_pTexture, &l_pSRV, l_Dim, o_Width, o_Height, o_Depth, o_Mipmap, l_LayerCount, l_Format);
        if (pLoadTex)
        {
            if (l_Dim != D3D11_RESOURCE_DIMENSION_TEXTURE2D)
            {
                UnloadDDSTexture(pLoadTex);
                return R_FAILED;
            }
            else
            {
                o_TexData.m_pTexture = (ID3D11Texture2D*)l_pTexture;
                o_TexData.m_pShaderResourceView = l_pSRV;
                o_TexData.m_pRenderTargetView = DM_NULL;
                o_TexData.m_pEngineTextureData = pLoadTex;
                o_Format = getTwineFormat(l_Format);
                o_VideoMemSize = CalculateTextureSize(o_Width, o_Height, o_Depth, l_Format, o_Mipmap, 1);

                return R_SUCCESS;
            }
        }
        else
            return R_FAILED;
    }
    else
        return R_FAILED;
}

ID3D11Texture2D*            RCRenderer_DX11::getTexResourceDX11(OSTexture2D i_Tex)
{
    Int l_Index = i_Tex.getHandle();
    if(l_Index < 0 || l_Index >= k_MaxTextureRes)
        return DM_NULL;
    if(m_TextureResPool[l_Index].m_bFree)
        return DM_NULL;

    return m_TextureResPool[l_Index].m_TextureData.m_pTexture;
}

ID3D11ShaderResourceView*   RCRenderer_DX11::getTexShaderResourceViewDX11(OSTexture2D i_Tex)
{
    Int l_Index = i_Tex.getHandle();
    if(l_Index < 0 || l_Index >= k_MaxTextureRes)
        return DM_NULL;
    if(m_TextureResPool[l_Index].m_bFree)
        return DM_NULL;

    return m_TextureResPool[l_Index].m_TextureData.m_pShaderResourceView;
}

ID3D11ShaderResourceView* RCRenderer_DX11::getTex3DShaderResourceViewDX11(OSTexture3D i_Tex)
{
	Int l_Index = i_Tex.getHandle();
	if (l_Index < 0 || l_Index >= k_MaxTextureRes)
		return DM_NULL;
	if (m_Texture3DResPool[l_Index].m_bFree)
		return DM_NULL;

	return m_Texture3DResPool[l_Index].m_TextureData.m_pShaderResourceView;
}

ID3D11ShaderResourceView* RCRenderer_DX11::getTexCubeShaderResourceViewDX11(OSTextureCube i_Tex)
{
    Int l_Index = i_Tex.getHandle();
    if (l_Index < 0 || l_Index >= k_MaxTextureRes)
        return DM_NULL;
    if (m_TextureCubeResPool[l_Index].m_bFree)
        return DM_NULL;

    return m_TextureCubeResPool[l_Index].m_TextureData.m_pShaderResourceView;
}

ID3D11RenderTargetView*     RCRenderer_DX11::getTexRenderTargetViewDX11(OSTexture2D i_Tex)
{
    Int l_Index = i_Tex.getHandle();
    if(l_Index < 0 || l_Index >= k_MaxTextureRes)
        return DM_NULL;
    if(m_TextureResPool[l_Index].m_bFree)
        return DM_NULL;

    return m_TextureResPool[l_Index].m_TextureData.m_pRenderTargetView;
}

ID3D11DepthStencilView*     RCRenderer_DX11::getTexDepthStencilViewDX11(OSTexture2D i_Tex)
{
    Int l_Index = i_Tex.getHandle();
    if(l_Index < 0 || l_Index >= k_MaxTextureRes)
        return DM_NULL;
    if(m_TextureResPool[l_Index].m_bFree)
        return DM_NULL;

    return m_TextureResPool[l_Index].m_TextureData.m_pDepthStencilView;
}

ID3D11UnorderedAccessView*     RCRenderer_DX11::getTexUAVDX11(OSTexture2D i_Tex)
{
    Int l_Index = i_Tex.getHandle();
    if (l_Index < 0 || l_Index >= k_MaxTextureRes)
        return DM_NULL;
    if (m_TextureResPool[l_Index].m_bFree)
        return DM_NULL;

    return m_TextureResPool[l_Index].m_TextureData.m_pUAV;
}

DResult                     RCRenderer_DX11::executeBegin()
{
    m_TextureLoaded = m_CurTextureLoaded;
    m_TextureFromPool = 0;
    m_TextureCreated = m_CurTextureCreated;
    m_TextureImported = 0;
    return R_SUCCESS;
}

DResult                     RCRenderer_DX11::executeEnd()
{
    m_TextureLoaded = m_CurTextureLoaded;
    m_TextureCreated = m_CurTextureCreated;
    return R_SUCCESS;
}

DResult                     RCRenderer_DX11::getTextureUsedInfo(Int& o_TexLoaded, Int& o_TexFromPool, Int& o_TexCreated, Int& o_TexImported)
{
    o_TexLoaded = m_TextureLoaded;
    o_TexFromPool = m_TextureFromPool;
    o_TexCreated = m_TextureCreated;
    o_TexImported = m_TextureImported;
    return R_SUCCESS;
}

void*                       RCRenderer_DX11::LoadDDSTexture(ID3D11Device* i_pD3D11Device, const DString& i_FileName, ID3D11Resource** o_ppDX11Resource, ID3D11ShaderResourceView** o_ppDX11SRV, D3D11_RESOURCE_DIMENSION& o_Dim, Int& o_Width, Int& o_Height, Int& o_Depth, Int& o_Mipmap, Int& o_LayerCount, DXGI_FORMAT& o_Format)
{
    Int l_PoolIdx = -1;
    for (Int i = 0; i < k_DDSPoolSize; ++i)
    {
        if (m_DDSPool[i].m_RefCount > 0)
        {
            if (m_DDSPool[i].m_Path == i_FileName)
            {
                l_PoolIdx = i;
                break;
            }
        }
    }

    if (l_PoolIdx < 0)
    {
        for (Int i = 0; i < k_DDSPoolSize; ++i)
        {
            if (m_DDSPool[i].m_RefCount == 0)
            {
                DResult dr;
                HRESULT hr;
                DFile l_ImageFile(i_FileName.c_str(), DOME_GetExternalFS());
                dr = l_ImageFile.open(DM_FALSE);
                if (DM_FAIL(dr))
                    return DM_NULL;

                Int l_ImageFileLen = l_ImageFile.getLength();
                Char* l_pImageBuffer = (Char*)DOME_Alloc(l_ImageFileLen);
                dr = l_ImageFile.read(l_pImageBuffer, l_ImageFileLen);
                l_ImageFile.close();
                DOME_ASSERT(DM_SUCC(dr));

                ID3D11Resource* l_pTexture;
                ID3D11ShaderResourceView* l_pSRV;
                hr = DirectX::CreateDDSTextureFromMemory(i_pD3D11Device, (uint8_t*)l_pImageBuffer, l_ImageFileLen, &l_pTexture, &l_pSRV);
                DOME_Free(l_pImageBuffer);

                if (hr != S_OK)
                    return DM_NULL;

                D3D11_RESOURCE_DIMENSION l_Dim;
                l_pTexture->GetType(&l_Dim);
                m_DDSPool[i].m_Dimension = l_Dim;

                if (l_Dim == D3D11_RESOURCE_DIMENSION_TEXTURE2D)
                {
                    D3D11_TEXTURE2D_DESC l_TexDesc;
                    ((ID3D11Texture2D*)l_pTexture)->GetDesc(&l_TexDesc);
                    m_DDSPool[i].m_Width = l_TexDesc.Width;
                    m_DDSPool[i].m_Height = l_TexDesc.Height;
                    m_DDSPool[i].m_Depth = 1;
                    m_DDSPool[i].m_MipmapsCount = l_TexDesc.MipLevels;
                    m_DDSPool[i].m_LayerCount = l_TexDesc.ArraySize;
                    m_DDSPool[i].m_Format = l_TexDesc.Format;
                    m_DDSPool[i].m_bCubemap = l_TexDesc.MiscFlags & D3D11_RESOURCE_MISC_TEXTURECUBE;
                    m_DDSPool[i].m_pDX11Resource = l_pTexture;
                    m_DDSPool[i].m_pDX11SRV = l_pSRV;
                }
                else if (l_Dim == D3D11_RESOURCE_DIMENSION_TEXTURE3D)
                {
                    D3D11_TEXTURE3D_DESC l_TexDesc;
                    ((ID3D11Texture3D*)l_pTexture)->GetDesc(&l_TexDesc);
                    m_DDSPool[i].m_Width = l_TexDesc.Width;
                    m_DDSPool[i].m_Height = l_TexDesc.Height;
                    m_DDSPool[i].m_Depth = l_TexDesc.Depth;
                    m_DDSPool[i].m_MipmapsCount = l_TexDesc.MipLevels;
                    m_DDSPool[i].m_LayerCount = 1;
                    m_DDSPool[i].m_Format = l_TexDesc.Format;
                    m_DDSPool[i].m_bCubemap = DM_FALSE;
                    m_DDSPool[i].m_pDX11Resource = l_pTexture;
                    m_DDSPool[i].m_pDX11SRV = l_pSRV;
                }
                else
                    return DM_NULL;


                m_DDSPool[i].m_Path = i_FileName;

                l_PoolIdx = i;
                break;
            }
        }
    }

    if (l_PoolIdx < 0)
        return DM_NULL;

    m_DDSPool[l_PoolIdx].m_RefCount++;
    o_Width = m_DDSPool[l_PoolIdx].m_Width;
    o_Height = m_DDSPool[l_PoolIdx].m_Height;
    o_Depth = m_DDSPool[l_PoolIdx].m_Depth;
    o_Mipmap = m_DDSPool[l_PoolIdx].m_MipmapsCount;
    o_LayerCount = m_DDSPool[l_PoolIdx].m_LayerCount;
    o_Format = m_DDSPool[l_PoolIdx].m_Format;
    *o_ppDX11Resource = m_DDSPool[l_PoolIdx].m_pDX11Resource;
    *o_ppDX11SRV = m_DDSPool[l_PoolIdx].m_pDX11SRV;
    o_Dim = m_DDSPool[l_PoolIdx].m_Dimension;

    return (void*)(l_PoolIdx + 1);
}

void                        RCRenderer_DX11::UnloadDDSTexture(void* i_pData)
{
    Int l_PoolIdx = (Int)i_pData;
    l_PoolIdx -= 1;
    if (l_PoolIdx < 0 || l_PoolIdx >= k_DDSPoolSize)
    {
        DOME_ASSERT2(0, "Unload nonexist dds texture.");
        return;
    }

    if (m_DDSPool[l_PoolIdx].m_RefCount <= 0)
    {
        DOME_ASSERT2(0, "Unload null dds texture.");
        return;
    }

    m_DDSPool[l_PoolIdx].m_RefCount--;
    if (m_DDSPool[l_PoolIdx].m_RefCount == 0)
    {
        m_DDSPool[l_PoolIdx].m_Path = "";
        RC_RELEASE(m_DDSPool[l_PoolIdx].m_pDX11Resource);
        RC_RELEASE(m_DDSPool[l_PoolIdx].m_pDX11SRV);
    }
}


RC_NAMESPACE_END