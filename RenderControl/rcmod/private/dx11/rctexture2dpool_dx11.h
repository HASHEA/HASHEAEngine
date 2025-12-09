/*
    filename:       rctexture2dpool_dx11.h
    author:         Ming Dong
    date:           2016-MAY-19
    description:    
*/
#pragma once

#include "../../public/rcmod.h"
#include <rc/public/rc.h>

RC_NAMESPACE_BEGIN

class RCTexture2DPool_DX11
{
    struct TextureCell
    {
        Bool                        m_bFree;
        Int                         m_Width;
        Int                         m_Height;
        Int                         m_Mipmap;
        RCGPUDATAFORMAT             m_Format;
        RCBUFFUSAGE                 m_Usage;

        RCOSTextureData             m_TextureData;
    };
    typedef TArray<TextureCell, IDefaultMemManager, 32>     _TexturePool;
    typedef TArray<void*, IDefaultMemManager, 32>           _ExternalFramePool;

public:
    RCTexture2DPool_DX11(ID3D11Device* i_pDX11Device, IExternalTexturePool* i_pExternalTexturePool);

    ~RCTexture2DPool_DX11();

    void        reset();

    Int         getWorkingSurface(Int i_Width, Int i_Height, Int i_Mipmap, RCGPUDATAFORMAT i_Format, RCBUFFUSAGE i_Usage, RCOSTextureData& o_TexData, int WriteFlag);

    DResult     doneWithWorkingSurface(Int i_ID);

private:
    IExternalTexturePool*           m_pExternalTexturePool;
    ID3D11Device*                   m_pDX11Device;
    _TexturePool                    m_TexturePool;
    _ExternalFramePool              m_ExternalFramePool;
};



RC_NAMESPACE_END