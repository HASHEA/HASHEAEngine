#include "pch.h"
/*
    filename:       mdoperatorscenerender.cpp
    author:         Ming Dong
    date:           2016-JUN-28
    description:    
*/

#include "mdoperatorscenerenderdepthonly.h"
#include <rc/public/iexecuter.h>

#ifdef RC_PERF
#include "KG3D_FrameTimer.h"
#else
#define FRAMETIMER_BEGIN(a,b)
#define FRAMETIMER_END(a)
#endif


RC_NAMESPACE_BEGIN

// MDSceneRender($F4)

MDOperatorSceneRenderDepthOnly::MDOperatorSceneRenderDepthOnly()
    : m_OperatorName("MDSceneRenderDepthOnly")
{

}

MDOperatorSceneRenderDepthOnly::~MDOperatorSceneRenderDepthOnly()
{

}

/****************************
    FROM MDOperator class
****************************/
const DString&      MDOperatorSceneRenderDepthOnly::getOperatorName() const
{
    return m_OperatorName;
}

Bool                MDOperatorSceneRenderDepthOnly::isGpuOperator() const
{
    return DM_FALSE;
}

Int                 MDOperatorSceneRenderDepthOnly::getInputCount() const
{
    return 3;
}

DSimpleTypeID       MDOperatorSceneRenderDepthOnly::getInputTypeID(Int i_Index) const
{
    if(i_Index == 0) return RCGlobal::k_SimpleTypeID_OSTexture2D;
    return RCGlobal::k_SimpleTypeID_DVector4f;
}

Int                 MDOperatorSceneRenderDepthOnly::getOutputCount() const
{
    return 1;
}

DSimpleTypeID       MDOperatorSceneRenderDepthOnly::getOutputTypeID(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_Index == 0);
    DOME_ASSERT(i_ParamCount == 3);
    return RCGlobal::k_SimpleTypeID_OSTexture2D;
}

RCGPUDATAFORMAT     MDOperatorSceneRenderDepthOnly::getOutputTexFmt(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_Index == 0);
    DOME_ASSERT(i_ParamCount == 3);
    DOME_ASSERT(i_pParamList[0]->isTexture());
    RCGPUDATAFORMAT l_Format;
    DResult l_Result = i_pParamList[0]->getTextureFormat(l_Format);
    DOME_ASSERT(DM_SUCC(l_Result));
    return l_Format;
}

DResult             MDOperatorSceneRenderDepthOnly::calcOutputTexSize(Int i_Index, MDEffect* i_pMDEffect, DVector2i& o_Size, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_Index == 0);
    DOME_ASSERT(i_ParamCount == 3);
    DOME_ASSERT(i_pParamList[0]&& i_pParamList[0]->isTexture());
    DOME_ASSERT(i_pParamList[1]&& i_pParamList[1]->isFloat4());
    DOME_ASSERT(i_pParamList[2]&& i_pParamList[2]->isFloat4());
    

    DResult l_Result;
    DVector4f l_Size;

    l_Result = i_pParamList[0]->getTextureSize(o_Size);
    if(DM_SUCC(l_Result))
        return l_Result;

    l_Result = i_pParamList[1]->getFloat4(l_Size);
    DOME_ASSERT(DM_SUCC(l_Result));

    o_Size.x = Int(l_Size.x + 0.5f);
    o_Size.y = Int(l_Size.y + 0.5f);

    return R_SUCCESS;
}

/****************************
    FROM MDOperatorCpu class
****************************/
MDOperandPtr        MDOperatorSceneRenderDepthOnly::execute(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, IRP* o_pInputReleasePoint) const
{
	FRAMETIMER_BEGIN(FTT_RC_CAL_RENDERDEPTHONLY, FTT_RC_CAL_EXECUTE_EFFECTPASS);
    DOME_ASSERT(i_ParamCount == 3);
    DOME_ASSERT(i_pParamList[0]&& i_pParamList[0]->isTexture());
    DOME_ASSERT(i_pParamList[1]&& i_pParamList[1]->isFloat4());
    DOME_ASSERT(i_pParamList[2]&& i_pParamList[2]->isFloat4());
    
    RCEffectManager* l_pRCEffectMgr = i_pMDEffect->getRCEffect()->getEffectManager();
    RCRenderer* l_pRenderer = l_pRCEffectMgr->getRenderer();

    o_pInputReleasePoint[0] = IRP_INFINISHCALLBACK;
    o_pInputReleasePoint[1] = IRP_AFTEREXECUTE;
    o_pInputReleasePoint[2] = IRP_AFTEREXECUTE;

    DSimpleTypedValue* l_pDepthValue = i_pParamList[0]->getDataPtr();
    DSimpleTypedValue* l_pParamValue = i_pParamList[1]->getDataPtr();
    DSimpleTypedValue* l_pViewport = i_pParamList[2]->getDataPtr();
    DVector4f l_Params = l_pParamValue->getDVector4f();
    DVector4f l_Viewport = l_pViewport->getDVector4f();
    DVector2i l_RtSize;
    Int       l_RenderType = Int(l_Params.z+0.5);


	// set viewport
	calcOutputTexSize(0, i_pMDEffect, l_RtSize, i_ParamCount, i_pParamList);
	RCMOD_Float4 l_ViewPort(l_Viewport.x * l_RtSize.x, l_Viewport.y * l_RtSize.y, l_Viewport.z * l_RtSize.x, l_Viewport.w * l_RtSize.y);

	RCMOD_Texture l_DepthTexture;
	*((OSTexture2D*)l_DepthTexture.getPtr()) = l_pDepthValue->getValue<OSTexture2D>();

	// render
	const static DStringHash k_KEY_ScenePlugin("RCPI_Scene");
	RCPI_Scene* l_pScenePlugin = (RCPI_Scene*)l_pRCEffectMgr->getPlugin(k_KEY_ScenePlugin);

	l_pScenePlugin->renderMainCamera_SceneRender((RCOSRendererData*)l_pRenderer->getOSRendererData(), NULL, &l_DepthTexture, l_ViewPort, l_RenderType, 1.0f, 1.0f);

	FRAMETIMER_END(FTT_RC_CAL_RENDERDEPTHONLY);

	return i_pParamList[0];
}

DResult             MDOperatorSceneRenderDepthOnly::destroyResult(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, MDOperandPtr i_pResult) const
{
    return R_SUCCESS;
}


RC_NAMESPACE_END