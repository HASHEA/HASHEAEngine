/**********************************************************
 * File: mdoperatorfsr3.cpp
 * Author: yizhouhu
 * Date: 2024-Aug-19
 * Description:
 **********************************************************/
#include "pch.h"
#include "mdoperatorfsr3.h"
#include <rc/public/iexecuter.h>
#ifdef RC_PERF
#include"KG3D_FrameTimer.h"
#else
#define FRAMETIMER_BEGIN(a,b)
#define FRAMETIMER_END(a)
#endif

RC_NAMESPACE_BEGIN
static const int s_InputCount = 3;

MDOperatorFSR3::MDOperatorFSR3()
	:m_OperatorName("MDFSR3")
{
}

MDOperatorFSR3::~MDOperatorFSR3()
{

}

const DString& MDOperatorFSR3::getOperatorName()const
{
	return m_OperatorName;
}

Bool MDOperatorFSR3::isGpuOperator()const
{
	return DM_FALSE;
}

Int MDOperatorFSR3::getInputCount()const
{
	return s_InputCount;
}

Int MDOperatorFSR3::getOutputCount()const
{
	return 1;
}

DSimpleTypeID MDOperatorFSR3::getInputTypeID(Int i_Index)const
{
	DOME_ASSERT(i_Index < 0 || i_Index >= s_InputCount);
	DSimpleTypeID InputTypes[s_InputCount] =
	{
		RCGlobal::k_SimpleTypeID_OSTexture2D,
		RCGlobal::k_SimpleTypeID_OSTexture2D,
		RCGlobal::k_SimpleTypeID_OSTexture2D,
	};
	return InputTypes[i_Index];
}

DSimpleTypeID MDOperatorFSR3::getOutputTypeID(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList)const
{
	return RCGlobal::k_SimpleTypeID_OSTexture2D;
}

RCGPUDATAFORMAT MDOperatorFSR3::getOutputTexFmt(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList)const
{
	return RGDF_RGBA8;
}

DResult MDOperatorFSR3::calcOutputTexSize(Int i_Index, MDEffect* i_pMDEffect, DVector2i& o_Size, Int i_ParamCount, const MDOperandCPtr* i_pParamList)const
{
	DOME_ASSERT(i_Index == 0);

	DVector4f l_Size;
	i_pMDEffect->getRCEffect()->getEffectManager()->getParamSys().getDVector4f(DStringHash("EXTERN::PARAMETER::OutputSize"), l_Size);

	o_Size.x = Int(l_Size.x + 0.1f);
	o_Size.y = Int(l_Size.y + 0.1f);

	return R_SUCCESS;
}

MDOperandPtr MDOperatorFSR3::execute(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, IRP* o_pInputReleasePoint)const
{
	DOME_ASSERT(i_ParamCount == s_InputCount);
	DOME_ASSERT(i_pParamList[0] && i_pParamList[0]->isTexture());
	DOME_ASSERT(i_pParamList[1] && i_pParamList[1]->isTexture());
	DOME_ASSERT(i_pParamList[2] && i_pParamList[2]->isTexture());

	DResult l_Result;
	RCEffectManager* l_pRCEffectMgr = i_pMDEffect->getRCEffect()->getEffectManager();
	RCRenderer* l_pRenderer = l_pRCEffectMgr->getRenderer();

	o_pInputReleasePoint[0] = IRP_INFINISHCALLBACK;
	o_pInputReleasePoint[1] = IRP_INFINISHCALLBACK;
	o_pInputReleasePoint[2] = IRP_INFINISHCALLBACK;

	DSimpleTypedValue* l_pColorTextureValue = i_pParamList[0]->getDataPtr();
	DSimpleTypedValue* l_pDepthTextureValue = i_pParamList[1]->getDataPtr();
	DSimpleTypedValue* l_pMotionVectorsTextureValue = i_pParamList[2]->getDataPtr();

	DVector2i l_RtSize;
	l_Result = calcOutputTexSize(0, i_pMDEffect, l_RtSize, i_ParamCount, i_pParamList);
	DOME_ASSERT(DM_SUCC(l_Result));

	RCGPUDATAFORMAT l_Format = getOutputTexFmt(0, i_pMDEffect, i_ParamCount, i_pParamList);
	DOME_ASSERT(l_Format == RGDF_RGBA32F || l_Format == RGDF_RGBA8);

	OSTexture2D l_OutputTex;
	l_Result = l_pRenderer->createTexture2D(l_OutputTex, l_RtSize.x, l_RtSize.y, 1, l_Format, RBU_DEFAULT, DM_TRUE, DM_NULL);
	DOME_ASSERT(DM_SUCC(l_Result));

	RCMOD_Texture l_OutputTexture;
	*((OSTexture2D*)l_OutputTexture.getPtr()) = l_OutputTex;

	RCMOD_Texture l_ColorInputTexture;
	*((OSTexture2D*)l_ColorInputTexture.getPtr()) = l_pColorTextureValue->getValue<OSTexture2D>();

	RCMOD_Texture l_DepthInputTexture;
	*((OSTexture2D*)l_DepthInputTexture.getPtr()) = l_pDepthTextureValue->getValue<OSTexture2D>();

	RCMOD_Texture l_MotionVectorsInputTexture;
	*((OSTexture2D*)l_MotionVectorsInputTexture.getPtr()) = l_pMotionVectorsTextureValue->getValue<OSTexture2D>();

	const static DStringHash k_KEY_ScenePlugin("RCPI_Scene");
	RCPI_Scene* l_pScenePlugin = (RCPI_Scene*)l_pRCEffectMgr->getPlugin(k_KEY_ScenePlugin);

	l_pScenePlugin->RenderFSR3((RCOSRendererData*)l_pRenderer->getOSRendererData(), &l_ColorInputTexture, &l_DepthInputTexture, &l_MotionVectorsInputTexture, nullptr, &l_OutputTexture);

	MDOperandValue* l_pRtOperand = DOME_New(MDOperandValue)(RCGlobal::k_SimpleTypeID_OSTexture2D);

	l_Result = l_pRtOperand->getDataPtr()->set(RCGlobal::k_SimpleTypeID_OSTexture2D, &l_OutputTex);
	DOME_ASSERT(DM_SUCC(l_Result));

	return l_pRtOperand;
}

DResult MDOperatorFSR3::destroyResult(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, MDOperandPtr i_pResult)const
{
	RCRenderer* l_pRenderer = i_pMDEffect->getRCEffect()->getEffectManager()->getRenderer();

	OSTexture2D l_hTexture;
	l_hTexture = *i_pResult->getTexturePtr();
	l_pRenderer->destroyTexture2D(l_hTexture);
	DOME_Del(i_pResult);

	return R_SUCCESS;
}
RC_NAMESPACE_END