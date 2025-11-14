#include "pch.h"
/*
    filename:       rctexture2dpool_dx11.cpp
    author:         Ming Dong
    date:           2016-MAY-19
    description:    
*/

#include "rctexture2dpool_dx11.h"
#include "rcrenderer_dx11.h"

#include "DevEnv/Internal/DirectXTex/D3DStats.h"

RC_NAMESPACE_BEGIN

RCTexture2DPool_DX11::RCTexture2DPool_DX11(ID3D11Device* i_pDX11Device, IExternalTexturePool* i_pExternalTexturePool)
{
    m_pExternalTexturePool = i_pExternalTexturePool;
    m_pDX11Device = i_pDX11Device;
}

RCTexture2DPool_DX11::~RCTexture2DPool_DX11()
{
    reset();
}

void        RCTexture2DPool_DX11::reset()
{
    if (m_pExternalTexturePool)
    {
        for (Int i = 0; i < m_ExternalFramePool.size(); ++i)
        {
            if (m_ExternalFramePool[i])
            {
                m_pExternalTexturePool->doneWithFrameBuffer(m_ExternalFramePool[i]);
                m_ExternalFramePool[i] = DM_NULL;
            }
            m_ExternalFramePool.clear();
        }
    }
    else
    {
        for (Int i = 0; i < m_TexturePool.size(); ++i)
        {
            TextureCell& l_Cell = m_TexturePool[i];
            DOME_ASSERT(l_Cell.m_bFree);

            RC_RELEASE(l_Cell.m_TextureData.m_pRenderTargetView);
            RC_RELEASE(l_Cell.m_TextureData.m_pShaderResourceView);
		    RC_RELEASE(l_Cell.m_TextureData.m_pDepthStencilView);
            RC_RELEASE(l_Cell.m_TextureData.m_pTexture);
        }
        m_TexturePool.clear();
    }
}

Int         RCTexture2DPool_DX11::getWorkingSurface(Int i_Width, Int i_Height, Int i_Mipmap, RCGPUDATAFORMAT i_Format, RCBUFFUSAGE i_Usage, RCOSTextureData& o_TexData, int i_WriteFlag)
{
    if (m_pExternalTexturePool)
    {
        Int i;
        for (i = 0; i < m_ExternalFramePool.size(); ++i)
        {
            if (m_ExternalFramePool[i] == DM_NULL)
                break;
        }
        if (i == m_ExternalFramePool.size())
            m_ExternalFramePool.push_back(DM_NULL);
        
        DXGI_FORMAT l_Format = getPlatformFormat(i_Format);
        m_ExternalFramePool[i] = m_pExternalTexturePool->getFrameBuffer(i_Width, i_Height, Math::Abs(i_Mipmap), l_Format, i_Mipmap < 0, i_WriteFlag);
        o_TexData.m_pTexture = m_pExternalTexturePool->getTextureResource(m_ExternalFramePool[i]);
        o_TexData.m_pRenderTargetView = m_pExternalTexturePool->getTextureRTV(m_ExternalFramePool[i]);
        o_TexData.m_pDepthStencilView = m_pExternalTexturePool->getTextureDSV(m_ExternalFramePool[i]);
        o_TexData.m_pShaderResourceView = m_pExternalTexturePool->getTextureSRV(m_ExternalFramePool[i]);
        o_TexData.m_pUAV = m_pExternalTexturePool->getTextureUAV(m_ExternalFramePool[i]);
        return i;
    }
    else
    {
        DResult l_Result;
        for (Int i = 0; i < m_TexturePool.size(); ++i)
        {
            TextureCell& l_Cell = m_TexturePool[i];
            if (l_Cell.m_bFree &&
                l_Cell.m_Width == i_Width &&
                l_Cell.m_Height == i_Height &&
                l_Cell.m_Mipmap == i_Mipmap &&
                l_Cell.m_Format == i_Format && 
                l_Cell.m_Usage == i_Usage)
            {
                l_Cell.m_bFree = DM_FALSE;
                o_TexData = l_Cell.m_TextureData;
                return i;
            }
        }

        TextureCell l_TexCell;
        l_TexCell.m_bFree = DM_FALSE;
        l_TexCell.m_Width = i_Width;
        l_TexCell.m_Height = i_Height;
        l_TexCell.m_Mipmap = i_Mipmap;
        l_TexCell.m_Format = i_Format;
        l_TexCell.m_Usage = i_Usage;

        l_Result = RCRenderer_DX11::DX11CreateTexture2D(o_TexData, m_pDX11Device, i_Width, i_Height, i_Mipmap, i_Format, i_Usage, DM_NULL);
        DOME_ASSERT(DM_SUCC(l_Result));

        l_TexCell.m_TextureData.m_pTexture = o_TexData.m_pTexture;
        l_TexCell.m_TextureData.m_pRenderTargetView = o_TexData.m_pRenderTargetView;
        l_TexCell.m_TextureData.m_pShaderResourceView = o_TexData.m_pShaderResourceView;
        l_TexCell.m_TextureData.m_pDepthStencilView = o_TexData.m_pDepthStencilView;

        m_TexturePool.push_back(l_TexCell);

        return m_TexturePool.size() - 1;
    }
}

DResult     RCTexture2DPool_DX11::doneWithWorkingSurface(Int i_ID)
{
    if (m_pExternalTexturePool)
    {
        DOME_ASSERT(i_ID >= 0 && i_ID < m_ExternalFramePool.size());
        DOME_ASSERT(m_ExternalFramePool[i_ID]);
        m_pExternalTexturePool->doneWithFrameBuffer(m_ExternalFramePool[i_ID]);
        m_ExternalFramePool[i_ID] = DM_NULL;
        return R_SUCCESS;
    }
    else
    {
        if(i_ID < 0 || i_ID >= m_TexturePool.size())
            return R_OUTOFRANGE;
        if(m_TexturePool[i_ID].m_bFree)
            return R_FAILED;
        m_TexturePool[i_ID].m_bFree = DM_TRUE;
        return R_SUCCESS;
    }
}


RC_NAMESPACE_END