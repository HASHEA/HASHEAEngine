#include "pch.h"
/*
    filename:       rcmod_dx11.cpp
    author:         Ming Dong
    date:           2016-MAY-13
    description:    
*/

#include "../../public/rcmod.h"
#include <rc/public/rc.h>
#include "rcrenderer_dx11.h"

RCMOD_API int  RCMOD_CreateEffectManager(const RCOSRendererData* i_pParam)
{
    RC_NS::RCRenderer_DX11* l_pRenderer = DOME_New(RC_NS::RCRenderer_DX11)(*i_pParam);
    return RC_NS::RCManager::Instance().createEffectManager(l_pRenderer);
}

RCMOD_API void RCMOD_DestroyEffectManager(int i_EffectMgrID)
{
    if (RCMOD_GetActiveEffectManager() == i_EffectMgrID)
    {
        RCMOD_SetActiveEffectManager(-1);
    }

    RC_NS::RCEffectManager* l_pEffectMgr = RC_NS::RCManager::Instance().getEffectManager(i_EffectMgrID);
    if (l_pEffectMgr)
    {
        RC_NS::RCRenderer_DX11* l_pRenderer = (RC_NS::RCRenderer_DX11*)l_pEffectMgr->getRenderer();
        RC_NS::RCManager::Instance().destroyEffectManager(i_EffectMgrID);
        DOME_Del(l_pRenderer);
    }
}

RCMOD_API ID3D11Texture2D*          RCMODDX11_GetTexResource(int i_EffectMgrID, const RCMOD_Texture& i_Tex)
{
    RC_NS::RCEffectManager* l_pEffectMgr = RC_NS::RCManager::Instance().getEffectManager(i_EffectMgrID);
    if (l_pEffectMgr)
    {
        RC_NS::RCRenderer_DX11* l_pRenderer = (RC_NS::RCRenderer_DX11*)l_pEffectMgr->getRenderer();

        return l_pRenderer->getTexResourceDX11(*((RC_NS::OSTexture2D*)i_Tex.getPtr()));
    }
    return DM_NULL;
}

RCMOD_API ID3D11ShaderResourceView* RCMODDX11_GetTexShaderResourceView(int i_EffectMgrID, const RCMOD_Texture& i_Tex)
{
    RC_NS::RCEffectManager* l_pEffectMgr = RC_NS::RCManager::Instance().getEffectManager(i_EffectMgrID);
    if (l_pEffectMgr)
    {
        RC_NS::RCRenderer_DX11* l_pRenderer = (RC_NS::RCRenderer_DX11*)l_pEffectMgr->getRenderer();

        return l_pRenderer->getTexShaderResourceViewDX11(*((RC_NS::OSTexture2D*)i_Tex.getPtr()));
    }
    return DM_NULL;
}

RCMOD_API ID3D11ShaderResourceView* RCMODDX11_GetTex3DShaderResourceView(int i_EffectMgrID, const RCMOD_Texture& i_Tex)
{
	RC_NS::RCEffectManager* l_pEffectMgr = RC_NS::RCManager::Instance().getEffectManager(i_EffectMgrID);
	if (l_pEffectMgr)
	{
		RC_NS::RCRenderer_DX11* l_pRenderer = (RC_NS::RCRenderer_DX11*)l_pEffectMgr->getRenderer();

		return l_pRenderer->getTex3DShaderResourceViewDX11(*((RC_NS::OSTexture3D*)i_Tex.getPtr()));
	}
	return DM_NULL;
}

RCMOD_API ID3D11ShaderResourceView* RCMODDX11_GetTexCubeShaderResourceView(int i_EffectMgrID, const RCMOD_Texture& i_Tex)
{
    RC_NS::RCEffectManager* l_pEffectMgr = RC_NS::RCManager::Instance().getEffectManager(i_EffectMgrID);
    if (l_pEffectMgr)
    {
        RC_NS::RCRenderer_DX11* l_pRenderer = (RC_NS::RCRenderer_DX11*)l_pEffectMgr->getRenderer();

        return l_pRenderer->getTexCubeShaderResourceViewDX11(*((RC_NS::OSTextureCube*)i_Tex.getPtr()));
    }
    return DM_NULL;
}

RCMOD_API ID3D11RenderTargetView*   RCMODDX11_GetTexRenderTargetView(int i_EffectMgrID, const RCMOD_Texture& i_Tex)
{
    RC_NS::RCEffectManager* l_pEffectMgr = RC_NS::RCManager::Instance().getEffectManager(i_EffectMgrID);
    if (l_pEffectMgr)
    {
        RC_NS::RCRenderer_DX11* l_pRenderer = (RC_NS::RCRenderer_DX11*)l_pEffectMgr->getRenderer();

        return l_pRenderer->getTexRenderTargetViewDX11(*((RC_NS::OSTexture2D*)i_Tex.getPtr()));
    }
    return DM_NULL;
}

RCMOD_API ID3D11DepthStencilView*   RCMODDX11_GetTexDepthStencilView(int i_EffectMgrID, const RCMOD_Texture& i_Tex)
{
    RC_NS::RCEffectManager* l_pEffectMgr = RC_NS::RCManager::Instance().getEffectManager(i_EffectMgrID);
    if (l_pEffectMgr)
    {
        RC_NS::RCRenderer_DX11* l_pRenderer = (RC_NS::RCRenderer_DX11*)l_pEffectMgr->getRenderer();

        return l_pRenderer->getTexDepthStencilViewDX11(*((RC_NS::OSTexture2D*)i_Tex.getPtr()));
    }
    return DM_NULL;
}

RCMOD_API ID3D11UnorderedAccessView*   RCMODDX11_GetTexUAV(int i_EffectMgrID, const RCMOD_Texture& i_Tex)
{
    RC_NS::RCEffectManager* l_pEffectMgr = RC_NS::RCManager::Instance().getEffectManager(i_EffectMgrID);
    if (l_pEffectMgr)
    {
        RC_NS::RCRenderer_DX11* l_pRenderer = (RC_NS::RCRenderer_DX11*)l_pEffectMgr->getRenderer();

        return l_pRenderer->getTexUAVDX11(*((RC_NS::OSTexture2D*)i_Tex.getPtr()));
    }
    return DM_NULL;
}

RCMOD_API bool  RCMOD_ExecuteBegin(int i_EffectMgrID)
{
    RC_NS::RCEffectManager* l_pEffectMgr = RC_NS::RCManager::Instance().getEffectManager(i_EffectMgrID);
    if (l_pEffectMgr)
    {
        RC_NS::RCRenderer_DX11* l_pRenderer = (RC_NS::RCRenderer_DX11*)l_pEffectMgr->getRenderer();

        RC_NS::DResult l_Result = l_pRenderer->executeBegin();
        if (DM_SUCC(l_Result))
            return true;
        else
            return false;
    }
    return false;
}

RCMOD_API bool  RCMOD_ExecuteEnd(int i_EffectMgrID)
{
    RC_NS::RCEffectManager* l_pEffectMgr = RC_NS::RCManager::Instance().getEffectManager(i_EffectMgrID);
    if (l_pEffectMgr)
    {
        RC_NS::RCRenderer_DX11* l_pRenderer = (RC_NS::RCRenderer_DX11*)l_pEffectMgr->getRenderer();

        RC_NS::DResult l_Result = l_pRenderer->executeEnd();
        if (DM_SUCC(l_Result))
            return true;
        else
            return false;
    }
    return false;
}

RCMOD_API bool  RCMOD_GetTextureUsedInfo(int i_EffectMgrID, double& o_TexLoaded, double& o_TexFromPool, double& o_TexCreated, double& o_TexImported)
{
    RC_NS::RCEffectManager* l_pEffectMgr = RC_NS::RCManager::Instance().getEffectManager(i_EffectMgrID);
    if (l_pEffectMgr)
    {
        RC_NS::RCRenderer_DX11* l_pRenderer = (RC_NS::RCRenderer_DX11*)l_pEffectMgr->getRenderer();

        RC_NS::Int l_TexLoaded, l_TexFromPool, l_TexCreated, l_TexImported;
        RC_NS::DResult l_Result = l_pRenderer->getTextureUsedInfo(l_TexLoaded, l_TexFromPool, l_TexCreated, l_TexImported);
        o_TexLoaded = l_TexLoaded;
        o_TexFromPool = l_TexFromPool;
        o_TexCreated = l_TexCreated;
        o_TexImported = l_TexImported;
        if (DM_SUCC(l_Result))
            return true;
        else
            return false;
    }
    return false;
}