#include "pch.h"

#include "MDOperatorPostUWater.h"
#include <rc/public/iexecuter.h>

#ifdef RC_PERF
#include "KG3D_FrameTimer.h"
#else
#define FRAMETIMER_BEGIN(a, b)
#define FRAMETIMER_END(a)
#endif

RC_NAMESPACE_BEGIN

static unsigned int nParamCount = 7;

MDOperatorPostUWater::MDOperatorPostUWater()
	: m_OperatorName("MDPostUWater")
{

}

MDOperatorPostUWater::~MDOperatorPostUWater()
{

}

/****************************
FROM MDOperator class
****************************/
const DString& MDOperatorPostUWater::getOperatorName() const
{
	return m_OperatorName;
}

Bool                MDOperatorPostUWater::isGpuOperator() const
{
	return DM_FALSE;
}

Int                 MDOperatorPostUWater::getInputCount() const
{
	return nParamCount;
}

DSimpleTypeID       MDOperatorPostUWater::getInputTypeID(Int i_Index) const
{
	return RCGlobal::k_SimpleTypeID_OSTexture2D;
}

Int                 MDOperatorPostUWater::getOutputCount() const
{
	return 1;
}

DSimpleTypeID       MDOperatorPostUWater::getOutputTypeID(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
	DOME_ASSERT(i_ParamCount == nParamCount);
	return RCGlobal::k_SimpleTypeID_OSTexture2D;
}

RCGPUDATAFORMAT     MDOperatorPostUWater::getOutputTexFmt(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
	DOME_ASSERT(i_ParamCount == nParamCount);
	DOME_ASSERT(i_pParamList[0] && i_pParamList[0]->isTexture());

	RCGPUDATAFORMAT l_Format;
	DResult l_Result;
	l_Result = i_pParamList[0]->getTextureFormat(l_Format);
	DOME_ASSERT(DM_SUCC(l_Result));

	return l_Format;
}

DResult             MDOperatorPostUWater::calcOutputTexSize(Int i_Index, MDEffect* i_pMDEffect, DVector2i& o_Size, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
	DOME_ASSERT(i_ParamCount == nParamCount);
	DOME_ASSERT(i_pParamList[0] && i_pParamList[0]->isTexture());

	DResult l_Result;
	l_Result = i_pParamList[0]->getTextureSize(o_Size);

	return l_Result;
}

/****************************
FROM MDOperatorCpu class
****************************/
MDOperandPtr        MDOperatorPostUWater::execute(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, IRP* o_pInputReleasePoint) const
{
	DOME_ASSERT(i_ParamCount == nParamCount);
	DOME_ASSERT(i_pParamList[0] && i_pParamList[0]->isTexture());
	DOME_ASSERT(i_pParamList[1] && i_pParamList[1]->isTexture());
	DOME_ASSERT(i_pParamList[2] && i_pParamList[2]->isTexture());
	DOME_ASSERT(i_pParamList[3] && i_pParamList[3]->isTexture());
	DOME_ASSERT(i_pParamList[4] && i_pParamList[4]->isTexture());
	DOME_ASSERT(i_pParamList[5] && i_pParamList[5]->isFloat4());
	DOME_ASSERT(i_pParamList[6] && i_pParamList[6]->isMatrix4x4());

	DResult l_Result;
	RCEffectManager* l_pEffectMgr = i_pMDEffect->getRCEffect()->getEffectManager();
	RCRenderer* l_pRenderer = l_pEffectMgr->getRenderer();

	o_pInputReleasePoint[0] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[1] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[2] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[3] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[4] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[5] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[6] = IRP_AFTEREXECUTE;

	DSimpleTypedValue* l_pColorValue		= i_pParamList[0]->getDataPtr();
	DSimpleTypedValue* l_pDepthValue		= i_pParamList[1]->getDataPtr();
	DSimpleTypedValue* l_pNormalValue		= i_pParamList[2]->getDataPtr();
	DSimpleTypedValue* l_pWaterMaskValue	= i_pParamList[3]->getDataPtr();
	DSimpleTypedValue* l_pColorGradValue	= i_pParamList[4]->getDataPtr();
	DSimpleTypedValue* l_pFogColorValue		= i_pParamList[5]->getDataPtr();
	DSimpleTypedValue* l_pParmsValue		= i_pParamList[6]->getDataPtr();

	RCMOD_Texture l_ColorTexture;
	*((OSTexture2D*)l_ColorTexture.getPtr()) = l_pColorValue->getValue<OSTexture2D>();

	RCMOD_Texture l_DepthTexture;
	*((OSTexture2D*)l_DepthTexture.getPtr()) = l_pDepthValue->getValue<OSTexture2D>();

	RCMOD_Texture l_NormalTexture;
	*((OSTexture2D*)l_NormalTexture.getPtr()) = l_pNormalValue->getValue<OSTexture2D>();

	RCMOD_Texture l_WaterMaskTexture;
	*((OSTexture2D*)l_WaterMaskTexture.getPtr()) = l_pWaterMaskValue->getValue<OSTexture2D>();

	RCMOD_Texture l_ColorGradTexture;
	*((OSTexture2D*)l_ColorGradTexture.getPtr()) = l_pColorGradValue->getValue<OSTexture2D>();

	RCMOD_Float4 l_FogColor;
	*(DVector4f*)l_FogColor.getPtr() = (l_pFogColorValue->getDVector4f());

	RCMOD_Float4x4 l_Params;
	*(DMatrix4x4f*)l_Params.getPtr() = (l_pParmsValue->getDMatrix4x4f());

	DVector2i l_RtSize;
	OSTexture2D l_RtTex;
	RCGPUDATAFORMAT l_Format = getOutputTexFmt(0, i_pMDEffect, i_ParamCount, i_pParamList);
	l_Result = calcOutputTexSize(0, i_pMDEffect, l_RtSize, i_ParamCount, i_pParamList);
	DOME_ASSERT(DM_SUCC(l_Result));
	l_Result = l_pRenderer->createTexture2D(l_RtTex, l_RtSize.x, l_RtSize.y, 1, l_Format, RBU_DEFAULT, DM_TRUE, DM_NULL);
	DOME_ASSERT(DM_SUCC(l_Result));
	RCMOD_Texture l_RTTexture;
	*((OSTexture2D*)l_RTTexture.getPtr()) = l_RtTex;


	// render
	const static DStringHash k_KEY_ScenePlugin("RCPI_Scene");
	RCPI_Scene* l_pScenePlugin = (RCPI_Scene*)l_pEffectMgr->getPlugin(k_KEY_ScenePlugin);

	l_pScenePlugin->RenderPrePostUWater(
		(RCOSRendererData*)l_pRenderer->getOSRendererData(),
		&l_RTTexture,
		&l_ColorTexture,
		&l_DepthTexture,
		&l_NormalTexture,
		&l_WaterMaskTexture,
		&l_ColorGradTexture,
		&l_FogColor,
		&l_Params,
		FALSE
	);

	MDOperandValue* l_pRtOperand = DOME_New(MDOperandValue)(RCGlobal::k_SimpleTypeID_OSTexture2D);
	l_Result = l_pRtOperand->getDataPtr()->set(RCGlobal::k_SimpleTypeID_OSTexture2D, &l_RtTex);
	DOME_ASSERT(DM_SUCC(l_Result));
	return l_pRtOperand;
}

DResult             MDOperatorPostUWater::destroyResult(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, MDOperandPtr i_pResult) const
{
	RCRenderer* l_pRenderer = i_pMDEffect->getRCEffect()->getEffectManager()->getRenderer();

	OSTexture2D l_hTexture;
	l_hTexture = *i_pResult->getTexturePtr();
	l_pRenderer->destroyTexture2D(l_hTexture);
	DOME_Del(i_pResult);

	return R_SUCCESS;
}

RC_NAMESPACE_END