#include "pch.h"
/*
    filename:       mdoperatorscenerender.cpp
    author:         Ming Dong
    date:           2016-JUN-28
    description:    
*/

#include "mdoperatorrendershadowmask.h"
#include <rc/public/iexecuter.h>

RC_NAMESPACE_BEGIN

MDOperatorRenderShadowMask::MDOperatorRenderShadowMask()
    : m_OperatorName("MDRenderShadowMask")
{

}

MDOperatorRenderShadowMask::~MDOperatorRenderShadowMask()
{

}

/****************************
    FROM MDOperator class
****************************/
const DString&      MDOperatorRenderShadowMask::getOperatorName() const
{
    return m_OperatorName;
}

Bool                MDOperatorRenderShadowMask::isGpuOperator() const
{
    return DM_FALSE;
}

Int                 MDOperatorRenderShadowMask::getInputCount() const
{
    return 1;
}

DSimpleTypeID       MDOperatorRenderShadowMask::getInputTypeID(Int i_Index) const
{
    DOME_ASSERT(i_Index == 0);
    return RCGlobal::k_SimpleTypeID_OSTexture2D;
}

Int                 MDOperatorRenderShadowMask::getOutputCount() const
{
    return 1;
}

DSimpleTypeID       MDOperatorRenderShadowMask::getOutputTypeID(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_ParamCount == 1);
    DOME_ASSERT(i_Index == 0);
    return RCGlobal::k_SimpleTypeID_OSTexture2D;
}

RCGPUDATAFORMAT     MDOperatorRenderShadowMask::getOutputTexFmt(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
	DOME_ASSERT(i_ParamCount == 1);
	DOME_ASSERT(i_Index == 0);
    
    return RGDF_RGBA8;
}

DResult             MDOperatorRenderShadowMask::calcOutputTexSize(Int i_Index, MDEffect* i_pMDEffect, DVector2i& o_Size, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
	DOME_ASSERT(i_ParamCount == 1);

    DVector4f l_Size;
    i_pMDEffect->getRCEffect()->getEffectManager()->getParamSys().getDVector4f(DStringHash("EXTERN::PARAMETER::GBufferSize"), l_Size);

    o_Size.x = Int(l_Size.x + 0.1f);
    o_Size.y = Int(l_Size.y + 0.1f);

    return R_SUCCESS;
}

/****************************
    FROM MDOperatorCpu class
****************************/
MDOperandPtr        MDOperatorRenderShadowMask::execute(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, IRP* o_pInputReleasePoint) const
{
	DOME_ASSERT(i_ParamCount == 1);
	DOME_ASSERT(i_pParamList[0] && i_pParamList[0]->isTexture());
    

    o_pInputReleasePoint[0] = IRP_INFINISHCALLBACK;

    DResult l_Result;
    RCEffectManager* l_pRCEffectMgr = i_pMDEffect->getRCEffect()->getEffectManager();
    RCRenderer* l_pRenderer = l_pRCEffectMgr->getRenderer();

    DVector2i l_RtSize;
    l_Result = calcOutputTexSize(0, i_pMDEffect, l_RtSize, i_ParamCount, i_pParamList);
    DOME_ASSERT(DM_SUCC(l_Result));

    RCMOD_Texture l_DepthTexture;
    DSimpleTypedValue* l_pDepthTexture = i_pParamList[0]->getDataPtr();
    *((OSTexture2D*)l_DepthTexture.getPtr()) = l_pDepthTexture->getValue<OSTexture2D>();

	RCGPUDATAFORMAT l_Format = getOutputTexFmt(0, i_pMDEffect, i_ParamCount, i_pParamList);
	DOME_ASSERT(l_Format == RGDF_RGBA8 || l_Format == RGDF_RGBA16F);

	OSTexture2D l_RtTex;
	l_Result = l_pRenderer->createTexture2D(l_RtTex, l_RtSize.x, l_RtSize.y, 1, l_Format, RBU_DEFAULT, DM_TRUE, DM_NULL);
	DOME_ASSERT(DM_SUCC(l_Result));

	// render
	const static DStringHash k_KEY_ScenePlugin("RCPI_Scene");
	RCPI_Scene* l_pScenePlugin = (RCPI_Scene*)l_pRCEffectMgr->getPlugin(k_KEY_ScenePlugin);
    
	RCMOD_Texture l_ColorTexture;
	*((OSTexture2D*)l_ColorTexture.getPtr()) = l_RtTex;

	l_pScenePlugin->RenderWholeSceneShadowMask(
		(RCOSRendererData*)l_pRenderer->getOSRendererData(),
        &l_DepthTexture,
		&l_ColorTexture
		);

	MDOperandValue* l_pRtOperand = DOME_New(MDOperandValue)(RCGlobal::k_SimpleTypeID_OSTexture2D);
	l_Result = l_pRtOperand->getDataPtr()->set(RCGlobal::k_SimpleTypeID_OSTexture2D, &l_RtTex);
	DOME_ASSERT(DM_SUCC(l_Result));

    return l_pRtOperand;
}

DResult             MDOperatorRenderShadowMask::destroyResult(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, MDOperandPtr i_pResult) const
{
	RCRenderer* l_pRenderer = i_pMDEffect->getRCEffect()->getEffectManager()->getRenderer();

	OSTexture2D l_hTexture;
	l_hTexture = *i_pResult->getTexturePtr();
	l_pRenderer->destroyTexture2D(l_hTexture);
	DOME_Del(i_pResult);

	return R_SUCCESS;
}


RC_NAMESPACE_END