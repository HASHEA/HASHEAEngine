#include "pch.h"
#include "mdoperatorairender.h"
#include <rc/public/iexecuter.h>
#include "../../../DevEnv/Include/PerfAnalyzer.h"

#ifdef RC_PERF
#include "KG3D_FrameTimer.h"
#else
#define FRAMETIMER_BEGIN(a,b)
#define FRAMETIMER_END(a)
#endif


RC_NAMESPACE_BEGIN
static unsigned int nParamCount = 10;

MDOperatorAIRender::MDOperatorAIRender() :
	m_OperatorName("MDAIRender")
{
}

MDOperatorAIRender::~MDOperatorAIRender()
{
}

const DString& MDOperatorAIRender::getOperatorName() const
{
	return m_OperatorName;
}

Bool MDOperatorAIRender::isGpuOperator() const
{
	return false;
}

Int MDOperatorAIRender::getInputCount() const
{
	return nParamCount;
}

DSimpleTypeID MDOperatorAIRender::getInputTypeID(Int i_Index) const
{
	DSimpleTypeID l_ParamTypeID;

	switch (i_Index)
	{
	case 0:
		l_ParamTypeID = RCGlobal::k_SimpleTypeID_OSTexture2D;
		break;
	case 2:
		l_ParamTypeID = RCGlobal::k_SimpleTypeID_F32;
		break;
	case 1:
		l_ParamTypeID = RCGlobal::k_SimpleTypeID_OSTexture2D;
		break;
	case 3:
		l_ParamTypeID = RCGlobal::k_SimpleTypeID_F32;
		break;
	default:
		l_ParamTypeID = RCGlobal::k_SimpleTypeID_OSTexture2D;
		break;
	}

	return l_ParamTypeID;
}

Int MDOperatorAIRender::getOutputCount() const
{
	return 1;
}

DSimpleTypeID MDOperatorAIRender::getOutputTypeID(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
	return RCGlobal::k_SimpleTypeID_OSTexture2D;
}

RCGPUDATAFORMAT MDOperatorAIRender::getOutputTexFmt(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
	DOME_ASSERT(i_ParamCount == nParamCount);
	DOME_ASSERT(i_pParamList[0] && i_pParamList[0]->isTexture());

	RCGPUDATAFORMAT l_Format = {};
	DResult l_Result = {};
	if (i_Index == 0)
	{
		l_Result = i_pParamList[0]->getTextureFormat(l_Format);
		DOME_ASSERT(DM_SUCC(l_Result));
		return l_Format;
	}
	else
		return RGDF_UNKNOWN;
}

DResult MDOperatorAIRender::calcOutputTexSize(Int i_Index, MDEffect* i_pMDEffect, DVector2i& o_Size, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
	DOME_ASSERT(i_ParamCount == nParamCount);
	DOME_ASSERT(i_pParamList[0] && i_pParamList[0]->isTexture());

	if (i_Index == 0)
	{
		return i_pParamList[0]->getTextureSize(o_Size);
	}
	else
		return R_FAILED;
}

MDOperandPtr MDOperatorAIRender::execute(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, IRP* o_pInputReleasePoint) const
{

	PERF_COUNTER_EX(0);
	FRAMETIMER_BEGIN(FTT_RC_CAL_AI_RENDER, FTT_RC_CAL_EXECUTE_EFFECTPASS);
	DOME_ASSERT(i_ParamCount == nParamCount);
	DOME_ASSERT(i_pParamList[0] && i_pParamList[0]->isTexture());           //color
	DOME_ASSERT(i_pParamList[1] && i_pParamList[1]->isTexture());           //NORMAL
	DOME_ASSERT(i_pParamList[2] && i_pParamList[2]->isFloat());				//num
	DOME_ASSERT(i_pParamList[3] && i_pParamList[3]->isFloat());				//type
	DOME_ASSERT(i_pParamList[4] && i_pParamList[4]->isTexture());           //G0
	DOME_ASSERT(i_pParamList[5] && i_pParamList[5]->isTexture());           //G1
	DOME_ASSERT(i_pParamList[6] && i_pParamList[6]->isTexture());           //G2
	DOME_ASSERT(i_pParamList[7] && i_pParamList[7]->isTexture());           //G3
	DOME_ASSERT(i_pParamList[8] && i_pParamList[8]->isTexture());           //G4
	DOME_ASSERT(i_pParamList[9] && i_pParamList[9]->isTexture());           //D


	RCEffectManager* l_pRCEffectMgr = i_pMDEffect->getRCEffect()->getEffectManager();
	RCRenderer* l_pRenderer = l_pRCEffectMgr->getRenderer();

	o_pInputReleasePoint[0] = IRP_INFINISHCALLBACK;
	o_pInputReleasePoint[1] = IRP_INFINISHCALLBACK;
	o_pInputReleasePoint[2] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[3] = IRP_AFTEREXECUTE;
	for (int i = 4; i < 10; i++)
	{
		o_pInputReleasePoint[i] = IRP_INFINISHCALLBACK;
	}

	DSimpleTypedValue* l_pColorSrcValue = i_pParamList[0]->getDataPtr();
	DSimpleTypedValue* l_pNormalSrcValue = i_pParamList[1]->getDataPtr();
	DSimpleTypedValue* l_pNum = i_pParamList[2]->getDataPtr();
	DSimpleTypedValue* l_type = i_pParamList[3]->getDataPtr();

	DSimpleTypedValue* l_pSrcValueArr[6] = {};
	for (int i = 0; i < 6; i++)
	{
		l_pSrcValueArr[i] = i_pParamList[4 + i]->getDataPtr();
	}

	RCMOD_Texture l_ColorSrcTexture;
	*static_cast<OSTexture2D*>(l_ColorSrcTexture.getPtr()) = l_pColorSrcValue->getValue<OSTexture2D>();
	RCMOD_Texture l_NormalSrcTexture;
	*static_cast<OSTexture2D*>(l_NormalSrcTexture.getPtr()) = l_pNormalSrcValue->getValue<OSTexture2D>();

	RCMOD_Texture l_SrcTextureArr[6] = {};
	for (int i = 0; i < 6; i++)
	{
		*static_cast<OSTexture2D*>(l_SrcTextureArr[i].getPtr()) = l_pSrcValueArr[i]->getValue<OSTexture2D>();
	}


	F32 picNum = l_pNum->getF32();
	F32 type = l_type->getF32();

	// render
	const static DStringHash k_KEY_ScenePlugin("RCPI_Scene");
	RCPI_Scene* l_pScenePlugin = static_cast<RCPI_Scene*>(l_pRCEffectMgr->getPlugin(k_KEY_ScenePlugin));

	l_pScenePlugin->RenderAIMode(static_cast<RCOSRendererData*>(l_pRenderer->getOSRendererData()), &l_ColorSrcTexture, &l_NormalSrcTexture, picNum, type, l_SrcTextureArr);

	FRAMETIMER_END(FTT_RC_CAL_AI_RENDER);

	return i_pParamList[0];
}

DResult MDOperatorAIRender::destroyResult(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, MDOperandPtr i_pResult) const
{
	return R_SUCCESS;
}

RC_NAMESPACE_END
