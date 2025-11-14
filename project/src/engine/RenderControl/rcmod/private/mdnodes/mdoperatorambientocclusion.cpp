#include "pch.h"
/*
    filename:       mdoperatorscenerender.cpp
    author:         Ming Dong
    date:           2016-JUN-28
    description:    
*/

#include "mdoperatorambientocclusion.h"
#include <rc/public/iexecuter.h>

#ifdef RC_PERF
#include "KG3D_FrameTimer.h"
#else
#define FRAMETIMER_BEGIN(a,b)
#define FRAMETIMER_END(a)
#endif


RC_NAMESPACE_BEGIN

// MDSceneRender($F4)
static const int s_InputCount = 4;
MDOperatorAmbientOcclusion::MDOperatorAmbientOcclusion()
    : m_OperatorName("MDAmbientOcclusion")
{

}

MDOperatorAmbientOcclusion::~MDOperatorAmbientOcclusion()
{

}

/****************************
    FROM MDOperator class
****************************/
const DString&      MDOperatorAmbientOcclusion::getOperatorName() const
{
    return m_OperatorName;
}

Bool                MDOperatorAmbientOcclusion::isGpuOperator() const
{
    return DM_FALSE;
}

Int                 MDOperatorAmbientOcclusion::getInputCount() const
{
    return s_InputCount;
}

DSimpleTypeID       MDOperatorAmbientOcclusion::getInputTypeID(Int i_Index) const
{
	DOME_ASSERT(i_Index < 0 || i_Index >= s_InputCount);
	DSimpleTypeID InputTypes[s_InputCount] = {
		RCGlobal::k_SimpleTypeID_OSTexture2D,
		RCGlobal::k_SimpleTypeID_OSTexture2D,
		RCGlobal::k_SimpleTypeID_DMatrix4x4f,
		RCGlobal::k_SimpleTypeID_DMatrix4x4f
	};
	return InputTypes[i_Index];
}

Int                 MDOperatorAmbientOcclusion::getOutputCount() const
{
    return 1;
}

DSimpleTypeID       MDOperatorAmbientOcclusion::getOutputTypeID(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    return RCGlobal::k_SimpleTypeID_OSTexture2D;
}

RCGPUDATAFORMAT     MDOperatorAmbientOcclusion::getOutputTexFmt(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
	return RGDF_RGBA16F;
}

DResult             MDOperatorAmbientOcclusion::calcOutputTexSize(Int i_Index, MDEffect* i_pMDEffect, DVector2i& o_Size, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
	DOME_ASSERT(i_Index == 0);
    DOME_ASSERT(i_ParamCount == s_InputCount);

    DResult l_Result;

    l_Result = i_pParamList[0]->getTextureSize(o_Size);
	DOME_ASSERT(DM_SUCC(l_Result));
    return R_SUCCESS;
}

/****************************
    FROM MDOperatorCpu class
****************************/
MDOperandPtr        MDOperatorAmbientOcclusion::execute(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, IRP* o_pInputReleasePoint) const
{
    PERF_COUNTER_EX(0);
	FRAMETIMER_BEGIN(FTT_RC_CAL_AO, FTT_RC_CAL_EXECUTE_EFFECTPASS);
    DOME_ASSERT(i_ParamCount == s_InputCount);
    DOME_ASSERT(i_pParamList[0] && i_pParamList[0]->isTexture());
	DOME_ASSERT(i_pParamList[1] && i_pParamList[1]->isTexture());
    DOME_ASSERT(i_pParamList[2] && i_pParamList[2]->isMatrix4x4());
    DOME_ASSERT(i_pParamList[3] && i_pParamList[3]->isMatrix4x4());
    
    DResult l_Result;
    RCEffectManager* l_pRCEffectMgr = i_pMDEffect->getRCEffect()->getEffectManager();
    RCRenderer* l_pRenderer = l_pRCEffectMgr->getRenderer();

    o_pInputReleasePoint[0] = IRP_AFTEREXECUTE;
    o_pInputReleasePoint[1] = IRP_AFTEREXECUTE;
    o_pInputReleasePoint[2] = IRP_AFTEREXECUTE;
    o_pInputReleasePoint[3] = IRP_AFTEREXECUTE;
	

	DSimpleTypedValue* l_pDepthValue		= i_pParamList[0]->getDataPtr();
	DSimpleTypedValue* l_pNormalValue		= i_pParamList[1]->getDataPtr();
    DSimpleTypedValue* l_pMatProjectValue	= i_pParamList[2]->getDataPtr();
    DSimpleTypedValue* l_pMatViewValue		= i_pParamList[3]->getDataPtr();
    
    DVector2i l_RtSize;
    l_Result = calcOutputTexSize(0, i_pMDEffect, l_RtSize, i_ParamCount, i_pParamList);
    DOME_ASSERT(DM_SUCC(l_Result));

    RCGPUDATAFORMAT l_Format = getOutputTexFmt(0, i_pMDEffect, i_ParamCount, i_pParamList);
    DOME_ASSERT(l_Format == RGDF_RGBA8 || l_Format == RGDF_RGBA16F);

    OSTexture2D l_RtTex;
    l_Result = l_pRenderer->createTexture2D(l_RtTex, l_RtSize.x, l_RtSize.y, 1, l_Format, RBU_DEFAULT, DM_TRUE, DM_NULL);
    DOME_ASSERT(DM_SUCC(l_Result));

	RCMOD_Texture l_ColorTexture;
	*((OSTexture2D*)l_ColorTexture.getPtr()) = l_RtTex;

	RCMOD_Texture l_DepthTexture;
	*((OSTexture2D*)l_DepthTexture.getPtr()) = l_pDepthValue->getValue<OSTexture2D>();

	RCMOD_Texture l_NormalTexture;
	*((OSTexture2D*)l_NormalTexture.getPtr()) = l_pNormalValue->getValue<OSTexture2D>();

	RCMOD_Float4x4 l_MatProject;
	*(DMatrix4x4f*)l_MatProject.getPtr() = (l_pMatProjectValue->getDMatrix4x4f());

	RCMOD_Float4x4 l_MatView;
	*(DMatrix4x4f*)l_MatView.getPtr() = (l_pMatViewValue->getDMatrix4x4f());
	// render
	const static DStringHash k_KEY_ScenePlugin("RCPI_Scene");
	RCPI_Scene* l_pScenePlugin = (RCPI_Scene*)l_pRCEffectMgr->getPlugin(k_KEY_ScenePlugin);
    
	l_pScenePlugin->RenderAmbientOcclusion((RCOSRendererData*)l_pRenderer->getOSRendererData(), &l_ColorTexture, &l_DepthTexture, &l_NormalTexture, l_MatProject, l_MatView);

    MDOperandValue* l_pRtOperand = DOME_New(MDOperandValue)(RCGlobal::k_SimpleTypeID_OSTexture2D);
    l_Result = l_pRtOperand->getDataPtr()->set(RCGlobal::k_SimpleTypeID_OSTexture2D, &l_RtTex);
    DOME_ASSERT(DM_SUCC(l_Result));
	FRAMETIMER_END(FTT_RC_CAL_AO);
    return l_pRtOperand;
}

DResult             MDOperatorAmbientOcclusion::destroyResult(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, MDOperandPtr i_pResult) const
{
	RCRenderer* l_pRenderer = i_pMDEffect->getRCEffect()->getEffectManager()->getRenderer();

	OSTexture2D l_hTexture;
	l_hTexture = *i_pResult->getTexturePtr();
	l_pRenderer->destroyTexture2D(l_hTexture);
	DOME_Del(i_pResult);

    return R_SUCCESS;
}


RC_NAMESPACE_END