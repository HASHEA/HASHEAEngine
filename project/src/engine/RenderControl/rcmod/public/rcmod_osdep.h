/*
    filename:       rcmod_osdep.h
    author:         Ming Dong
    date:           2016-Aug-22
    description:    
*/
#pragma once

#if RCRENDERERSYS == RCRENDERERSYS_DX11
#define WIN32_LEAN_AND_MEAN
#include "d3d11.h"
#include <string>

#include "rcmod_def.h"

class IExternalTexturePool
{
public:
    virtual void*                       getFrameBuffer(int i_Width, int i_Height, int i_Mipmap, DXGI_FORMAT i_Format, bool i_bGenMipmap, int i_WriteFlag = 0) = 0;
    virtual bool                        doneWithFrameBuffer(void* i_pFBuffer) = 0;
    virtual ID3D11Texture2D*            getTextureResource(void* i_pFBuffer) = 0;
    virtual ID3D11ShaderResourceView*   getTextureSRV(void* i_pFBuffer) = 0;
    virtual ID3D11RenderTargetView*     getTextureRTV(void* i_pFBuffer) = 0;
    virtual ID3D11DepthStencilView*     getTextureDSV(void* i_pFBuffer) = 0;
    virtual ID3D11UnorderedAccessView*  getTextureUAV(void* i_pFBuffer) = 0;
};

class IShaderCache
{
public:
	virtual void*                       getCacheShader(const char* i_pSignature, const GUID& i_SigGuid, void** o_ppBuffer, int* o_pLength, const char* i_pShaderContent, int i_Length, const char* szEntryPoint, ID3DInclude* piInclude, const char* szTarget) = 0;
    virtual void                        releaseCacheShader(void* i_pShader) = 0;

    virtual void*                       loadTextureFromFile(const char* i_pFile, ID3D11Resource** o_ppTexture, ID3D11ShaderResourceView** o_ppSRV) = 0;
    virtual void                        unloadTextureFile(void* i_pTexture) = 0;
    virtual void                        refreshLoadedTexture(void* i_pTexture, ID3D11Resource** o_ppTexture, ID3D11ShaderResourceView** o_ppSRV) = 0;
};

struct RCDX11RendererData
{
    ID3D11Device*               m_pDevice;
    ID3D11DeviceContext*        m_pDeviceContext;
    IExternalTexturePool*       m_pTexturePool;
    IShaderCache*               m_pShaderCache;
};
typedef RCDX11RendererData      RCOSRendererData;

struct RCDX11TextureData
{
    ID3D11Texture2D*            m_pTexture;
    ID3D11RenderTargetView*     m_pRenderTargetView;
    ID3D11ShaderResourceView*   m_pShaderResourceView;
    ID3D11DepthStencilView*     m_pDepthStencilView;
    ID3D11UnorderedAccessView*  m_pUAV;
    void*                       m_pEngineTextureData;

    RCDX11TextureData()
    {
        m_pTexture = NULL;
        m_pRenderTargetView = NULL;
        m_pShaderResourceView = NULL;
        m_pDepthStencilView = NULL;
        m_pUAV = NULL;
        m_pEngineTextureData = NULL;
    }
};
typedef RCDX11TextureData       RCOSTextureData;

//struct RCTexID
//{
//    int                         m_ID;
//};

typedef unsigned __int64 RCParamKey;

class RCMOD_Texture;

RCMOD_API ID3D11Texture2D*          RCMODDX11_GetTexResource(int i_EffectMgrID, const RCMOD_Texture& i_Tex);
RCMOD_API ID3D11ShaderResourceView* RCMODDX11_GetTexShaderResourceView(int i_EffectMgrID, const RCMOD_Texture& i_Tex);
RCMOD_API ID3D11ShaderResourceView* RCMODDX11_GetTex3DShaderResourceView(int i_EffectMgrID, const RCMOD_Texture& i_Tex);
RCMOD_API ID3D11ShaderResourceView* RCMODDX11_GetTexCubeShaderResourceView(int i_EffectMgrID, const RCMOD_Texture& i_Tex);
RCMOD_API ID3D11RenderTargetView*   RCMODDX11_GetTexRenderTargetView(int i_EffectMgrID, const RCMOD_Texture& i_Tex);
RCMOD_API ID3D11DepthStencilView*   RCMODDX11_GetTexDepthStencilView(int i_EffectMgrID, const RCMOD_Texture& i_Tex);
RCMOD_API ID3D11UnorderedAccessView* RCMODDX11_GetTexUAV(int i_EffectMgrID, const RCMOD_Texture& i_Tex);

#else
#error Your renderer system is not support now
#endif

