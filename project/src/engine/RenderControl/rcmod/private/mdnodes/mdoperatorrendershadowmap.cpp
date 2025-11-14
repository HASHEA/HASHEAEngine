#include "pch.h"
/*
    filename:       mdoperatorscenerender.cpp
    author:         Ming Dong
    date:           2016-JUN-28
    description:    
*/

#include "mdoperatorrendershadowmap.h"
#include <rc/public/iexecuter.h>

#ifdef RC_PERF
#include "KG3D_FrameTimer.h"
#else
#define FRAMETIMER_BEGIN(a,b)
#define FRAMETIMER_END(a)
#endif


RC_NAMESPACE_BEGIN

MDOperatorRenderShadowMap::MDOperatorRenderShadowMap()
    : m_OperatorName("MDRenderShadowMap")
{

}

MDOperatorRenderShadowMap::~MDOperatorRenderShadowMap()
{

}

/****************************
    FROM MDOperator class
****************************/
const DString&      MDOperatorRenderShadowMap::getOperatorName() const
{
    return m_OperatorName;
}

Bool                MDOperatorRenderShadowMap::isGpuOperator() const
{
    return DM_FALSE;
}

Int                 MDOperatorRenderShadowMap::getInputCount() const
{
    return 2;
}

DSimpleTypeID       MDOperatorRenderShadowMap::getInputTypeID(Int i_Index) const
{
    DOME_ASSERT(i_Index == 0 || i_Index == 1);
    return RCGlobal::k_SimpleTypeID_OSTexture2D;
}

Int                 MDOperatorRenderShadowMap::getOutputCount() const
{
    return 2;
}

DSimpleTypeID       MDOperatorRenderShadowMap::getOutputTypeID(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_ParamCount == 2);
    DOME_ASSERT(i_Index >= 0 && i_Index < 2);
    return RCGlobal::k_SimpleTypeID_OSTexture2D;
}

RCGPUDATAFORMAT     MDOperatorRenderShadowMap::getOutputTexFmt(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_ParamCount == 2);
    DOME_ASSERT(i_Index >= 0 && i_Index < 2);
    
    return RGDF_D24S8;
}

DResult             MDOperatorRenderShadowMap::calcOutputTexSize(Int i_Index, MDEffect* i_pMDEffect, DVector2i& o_Size, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
	DOME_ASSERT(i_ParamCount == 2);

    DVector4f l_Size;
    i_pMDEffect->getRCEffect()->getEffectManager()->getParamSys().getDVector4f(DStringHash("EXTERN::PARAMETER::GBufferSize"), l_Size);

    o_Size.x = Int(l_Size.x + 0.1f);
    o_Size.y = Int(l_Size.y + 0.1f);

    return R_SUCCESS;
}

/****************************
    FROM MDOperatorCpu class
****************************/
MDOperandPtr        MDOperatorRenderShadowMap::execute(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, IRP* o_pInputReleasePoint) const
{
	FRAMETIMER_BEGIN(FTT_RC_CAL_RENDERSHADOWMAP, FTT_RC_CAL_EXECUTE_EFFECTPASS);
	DOME_ASSERT(i_ParamCount == 2);
	DOME_ASSERT(i_pParamList[0] && i_pParamList[0]->isTexture());
    DOME_ASSERT(i_pParamList[1] && i_pParamList[1]->isTexture());

    o_pInputReleasePoint[0] = IRP_INFINISHCALLBACK;
    o_pInputReleasePoint[1] = IRP_INFINISHCALLBACK;

    DResult l_Result;
    RCEffectManager* l_pRCEffectMgr = i_pMDEffect->getRCEffect()->getEffectManager();
    RCRenderer* l_pRenderer = l_pRCEffectMgr->getRenderer();

    DVector2i l_RtSize;
    l_Result = calcOutputTexSize(0, i_pMDEffect, l_RtSize, i_ParamCount, i_pParamList);
    DOME_ASSERT(DM_SUCC(l_Result));

    RCMOD_Texture l_DepthTexture;
    DSimpleTypedValue* l_pDepthTexture = i_pParamList[0]->getDataPtr();
    *((OSTexture2D*)l_DepthTexture.getPtr()) = l_pDepthTexture->getValue<OSTexture2D>();


	// render
	const static DStringHash k_KEY_ScenePlugin("RCPI_Scene");
	RCPI_Scene* l_pScenePlugin = (RCPI_Scene*)l_pRCEffectMgr->getPlugin(k_KEY_ScenePlugin);
    
	l_pScenePlugin->RenderShadowMap(
		(RCOSRendererData*)l_pRenderer->getOSRendererData(),
        &l_DepthTexture
		);

	MDOperandArray* l_pOperand = DOME_New(MDOperandArray);
    l_pOperand->addOperand(i_pParamList[0]);
    l_pOperand->addOperand(i_pParamList[1]);


	FRAMETIMER_END(FTT_RC_CAL_RENDERSHADOWMAP);

    return l_pOperand;
}

DResult             MDOperatorRenderShadowMap::destroyResult(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, MDOperandPtr i_pResult) const
{
    DOME_Del(i_pResult);
    return R_SUCCESS;
}


RC_NAMESPACE_END