#include "pch.h"

#include "mdoperatordlss2upscale.h"
#include <rc/public/iexecuter.h>

#ifdef RC_PERF
#include "KG3D_FrameTimer.h"
#else
#define FRAMETIMER_BEGIN(a,b)
#define FRAMETIMER_END(a)
#endif


RC_NAMESPACE_BEGIN

static const unsigned int nParamCount = 6;
MDOperatorDLSS2UpScale::MDOperatorDLSS2UpScale()
	: m_OperatorName("MDDLSS2")
{

}

MDOperatorDLSS2UpScale::~MDOperatorDLSS2UpScale()
{

}

const DString& MDOperatorDLSS2UpScale::getOperatorName() const
{
	return m_OperatorName;
}

Bool                MDOperatorDLSS2UpScale::isGpuOperator() const
{
	return DM_FALSE;
}

Int                 MDOperatorDLSS2UpScale::getInputCount() const
{
	return nParamCount;
}

DSimpleTypeID       MDOperatorDLSS2UpScale::getInputTypeID(Int i_Index) const
{
	DOME_ASSERT(i_Index < 0 || i_Index >= nParamCount);
	DSimpleTypeID InputTypes[nParamCount] =
	{
		RCGlobal::k_SimpleTypeID_OSTexture2D,
		RCGlobal::k_SimpleTypeID_OSTexture2D,
		RCGlobal::k_SimpleTypeID_OSTexture2D,
		RCGlobal::k_SimpleTypeID_OSTexture2D,
		RCGlobal::k_SimpleTypeID_F32,
		RCGlobal::k_SimpleTypeID_F32
	};

	return InputTypes[i_Index];

}

Int                 MDOperatorDLSS2UpScale::getOutputCount() const
{
	return 1;
}

DSimpleTypeID       MDOperatorDLSS2UpScale::getOutputTypeID(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
	return RCGlobal::k_SimpleTypeID_OSTexture2D;
}

RCGPUDATAFORMAT     MDOperatorDLSS2UpScale::getOutputTexFmt(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
	DOME_ASSERT(i_ParamCount == nParamCount);
	DOME_ASSERT(i_pParamList[0] && i_pParamList[0]->isTexture());	
	//DOME_ASSERT(i_pParamList[1] && i_pParamList[1]->isTexture());
	//DOME_ASSERT(i_pParamList[2] && i_pParamList[2]->isTexture());
	//DOME_ASSERT(i_pParamList[3] && i_pParamList[3]->isFloat());
	//DOME_ASSERT(i_pParamList[4] && i_pParamList[4]->isFloat());

	DResult l_Result;
	RCGPUDATAFORMAT l_Format;
	l_Result = i_pParamList[0]->getTextureFormat(l_Format);
	DOME_ASSERT(DM_SUCC(l_Result));
	return l_Format;
}

DResult             MDOperatorDLSS2UpScale::calcOutputTexSize(Int i_Index, MDEffect* i_pMDEffect, DVector2i& o_Size, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
	DOME_ASSERT(i_ParamCount == nParamCount);
	DOME_ASSERT(i_pParamList[0] && i_pParamList[0]->isTexture());							

	DVector4f l_Size;
	i_pMDEffect->getRCEffect()->getEffectManager()->getParamSys().getDVector4f(DStringHash("EXTERN::PARAMETER::OutputSize"), l_Size);

	o_Size.x = Int(l_Size.x + 0.1f);
	o_Size.y = Int(l_Size.y + 0.1f);

	return R_SUCCESS;
}


MDOperandPtr        MDOperatorDLSS2UpScale::execute(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, IRP* o_pInputReleasePoint) const
{
	PERF_COUNTER_EX(0);
	FRAMETIMER_BEGIN(FTT_RC_CAL_SCENERENDER, FTT_RC_CAL_EXECUTE_EFFECTPASS);
	DOME_ASSERT(i_ParamCount == nParamCount);
	DOME_ASSERT(i_pParamList[0] && i_pParamList[0]->isTexture());
	DOME_ASSERT(i_pParamList[1] && i_pParamList[1]->isTexture());
	DOME_ASSERT(i_pParamList[2] && i_pParamList[2]->isTexture());
	DOME_ASSERT(i_pParamList[3] && i_pParamList[3]->isTexture());
	DOME_ASSERT(i_pParamList[4] && i_pParamList[4]->isFloat());
	DOME_ASSERT(i_pParamList[5] && i_pParamList[5]->isFloat());		

	DResult l_Result;
	RCEffectManager* l_pRCEffectMgr = i_pMDEffect->getRCEffect()->getEffectManager();
	RCRenderer* l_pRenderer = l_pRCEffectMgr->getRenderer();

	o_pInputReleasePoint[0] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[1] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[2] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[3] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[4] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[4] = IRP_AFTEREXECUTE;

	DSimpleTypedValue* l_pColorValue = i_pParamList[0]->getDataPtr();
	DSimpleTypedValue* l_pDepthValue = i_pParamList[1]->getDataPtr();
	DSimpleTypedValue* l_pMotionVectorValue = i_pParamList[2]->getDataPtr();
	DSimpleTypedValue* l_pExposureTexture = i_pParamList[3]->getDataPtr();
	DSimpleTypedValue* l_pSharpnessValue = i_pParamList[4]->getDataPtr();
	DSimpleTypedValue* l_pQualityValue = i_pParamList[5]->getDataPtr();


	DVector2i l_RtSize;
	RCGPUDATAFORMAT l_Format;

	l_Result = calcOutputTexSize(0, i_pMDEffect, l_RtSize, i_ParamCount, i_pParamList);
	DOME_ASSERT(DM_SUCC(l_Result));
	l_Format = getOutputTexFmt(0, i_pMDEffect, i_ParamCount, i_pParamList);
	DOME_ASSERT(l_Format == RGDF_RGBA8 || l_Format == RGDF_RGBA16F);

	OSTexture2D l_RtTex;
	l_Result = l_pRenderer->createTexture2D(l_RtTex, l_RtSize.x, l_RtSize.y, 1, l_Format, RBU_DEFAULT, DM_TRUE, DM_NULL, 1);
	DOME_ASSERT(DM_SUCC(l_Result));

	RCMOD_Texture l_OutputTexture;
	*((OSTexture2D*)l_OutputTexture.getPtr()) = l_RtTex;

	RCMOD_Texture l_ColorTexture;
	*((OSTexture2D*)l_ColorTexture.getPtr()) = l_pColorValue->getValue<OSTexture2D>();

	RCMOD_Texture l_DepthTexture;
	*((OSTexture2D*)l_DepthTexture.getPtr()) = l_pDepthValue->getValue<OSTexture2D>();

	RCMOD_Texture l_ExposureTexture;
	*((OSTexture2D*)l_ExposureTexture.getPtr()) = l_pExposureTexture->getValue<OSTexture2D>();

	RCMOD_Texture l_MotionVectorTexture;
	*((OSTexture2D*)l_MotionVectorTexture.getPtr()) = l_pMotionVectorValue->getValue<OSTexture2D>();

	float l_Sharpness;
	l_Sharpness = l_pSharpnessValue->getF32();

	float l_QualityValue;
	l_QualityValue = l_pQualityValue->getF32();


	const static DStringHash k_KEY_ScenePlugin("RCPI_Scene");
	RCPI_Scene* l_pScenePlugin = (RCPI_Scene*)l_pRCEffectMgr->getPlugin(k_KEY_ScenePlugin);
	l_pScenePlugin->renderMainCamera_DLSS2UpScale((RCOSRendererData*)l_pRenderer->getOSRendererData(), &l_ColorTexture, &l_DepthTexture, &l_MotionVectorTexture, &l_ExposureTexture, l_Sharpness, l_QualityValue, &l_OutputTexture);

	MDOperandValue* l_pRtOperand = DOME_New(MDOperandValue)(RCGlobal::k_SimpleTypeID_OSTexture2D);
	l_Result = l_pRtOperand->getDataPtr()->set(RCGlobal::k_SimpleTypeID_OSTexture2D, &l_RtTex);
	DOME_ASSERT(DM_SUCC(l_Result));

	FRAMETIMER_END(FTT_RC_CAL_SCENERENDER);

	return l_pRtOperand;
}

DResult             MDOperatorDLSS2UpScale::destroyResult(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, MDOperandPtr i_pResult) const
{
	RCRenderer* l_pRenderer = i_pMDEffect->getRCEffect()->getEffectManager()->getRenderer();

	OSTexture2D l_hTexture;
	l_hTexture = *i_pResult->getTexturePtr();
	l_pRenderer->destroyTexture2D(l_hTexture);

	DOME_Del(i_pResult);

	return R_SUCCESS;
}


RC_NAMESPACE_END