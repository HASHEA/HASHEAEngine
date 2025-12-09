#include "pch.h"
/*
	filename:       mdoperatorscenerender.cpp
	author:         Ming Dong
	date:           2016-JUN-28
	description:
*/

#include "mdoperatorrealoit.h"
#include <rc/public/iexecuter.h>
#include "../../../DevEnv/Include/PerfAnalyzer.h"

#ifdef RC_PERF
#include "KG3D_FrameTimer.h"
#else
#define FRAMETIMER_BEGIN(a,b)
#define FRAMETIMER_END(a)
#endif


RC_NAMESPACE_BEGIN

// MDSceneRender($F4)
static unsigned int nParamCount = 2;
MDOperatorRealOIT::MDOperatorRealOIT()
	: m_OperatorName("MDRealOIT")
{
	
}

MDOperatorRealOIT::~MDOperatorRealOIT()
{

}

/****************************
	FROM MDOperator class
****************************/
const DString& MDOperatorRealOIT::getOperatorName() const
{
	return m_OperatorName;
}

Bool                MDOperatorRealOIT::isGpuOperator() const
{
	return DM_FALSE;
}

Int                 MDOperatorRealOIT::getInputCount() const
{
	return nParamCount;
}

DSimpleTypeID       MDOperatorRealOIT::getInputTypeID(Int i_Index) const
{
	DSimpleTypeID l_ParamTypeID;

	switch (i_Index)
	{
	case 0:
	case 1:
		l_ParamTypeID = RCGlobal::k_SimpleTypeID_OSTexture2D;
		break;
	}

	return l_ParamTypeID;
}

Int      MDOperatorRealOIT::getOutputCount() const
{
	return 1;
}

DSimpleTypeID       MDOperatorRealOIT::getOutputTypeID(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
	DOME_ASSERT(i_ParamCount == nParamCount);

	return RCGlobal::k_SimpleTypeID_OSTexture2D;;
}

RCGPUDATAFORMAT     MDOperatorRealOIT::getOutputTexFmt(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
	DOME_ASSERT(i_ParamCount == nParamCount);
	DOME_ASSERT(i_pParamList[0] && i_pParamList[0]->isTexture());

	RCGPUDATAFORMAT l_Format;
	DResult l_Result;
	if (i_Index == 0)
	{
		l_Result = i_pParamList[0]->getTextureFormat(l_Format);
		DOME_ASSERT(DM_SUCC(l_Result));
		return l_Format;
	}
	else
		return RGDF_UNKNOWN;
}

DResult             MDOperatorRealOIT::calcOutputTexSize(Int i_Index, MDEffect* i_pMDEffect, DVector2i& o_Size, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
	DOME_ASSERT(i_ParamCount == nParamCount);
	DOME_ASSERT(i_pParamList[0] && i_pParamList[0]->isTexture());
	DOME_ASSERT(i_pParamList[1] && i_pParamList[1]->isTexture());

	if (i_Index == 0)
	{
		return i_pParamList[0]->getTextureSize(o_Size);
	}
	else
		return R_FAILED;
}

/****************************
	FROM MDOperatorCpu class
****************************/
MDOperandPtr        MDOperatorRealOIT::execute(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, IRP* o_pInputReleasePoint) const
{
	PERF_COUNTER_EX(0);
	FRAMETIMER_BEGIN(FTT_RC_CAL_OIT, FTT_RC_CAL_EXECUTE_EFFECTPASS);
	DOME_ASSERT(i_ParamCount == nParamCount);
	DOME_ASSERT(i_pParamList[0] && i_pParamList[0]->isTexture());           //color
	DOME_ASSERT(i_pParamList[1] && i_pParamList[1]->isTexture());           //depth

	RCEffectManager* l_pRCEffectMgr = i_pMDEffect->getRCEffect()->getEffectManager();
	RCRenderer* l_pRenderer = l_pRCEffectMgr->getRenderer();

	o_pInputReleasePoint[0] = IRP_INFINISHCALLBACK;
	o_pInputReleasePoint[1] = IRP_INFINISHCALLBACK;

	DSimpleTypedValue* l_pColorSrcValue = i_pParamList[0]->getDataPtr();
	DSimpleTypedValue* l_pDepthValue = i_pParamList[1]->getDataPtr();

	RCMOD_Texture l_ColorSrcTexture;
	*((OSTexture2D*)l_ColorSrcTexture.getPtr()) = l_pColorSrcValue->getValue<OSTexture2D>();

	RCMOD_Texture l_DepthTexture;
	*((OSTexture2D*)l_DepthTexture.getPtr()) = l_pDepthValue->getValue<OSTexture2D>();

	// render
	const static DStringHash k_KEY_ScenePlugin("RCPI_Scene");
	RCPI_Scene* l_pScenePlugin = (RCPI_Scene*)l_pRCEffectMgr->getPlugin(k_KEY_ScenePlugin);

	l_pScenePlugin->RenderRealOIT((RCOSRendererData*)l_pRenderer->getOSRendererData(), &l_ColorSrcTexture, &l_DepthTexture);

	FRAMETIMER_END(FTT_RC_CAL_OIT);

	return i_pParamList[0];
}

DResult             MDOperatorRealOIT::destroyResult(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, MDOperandPtr i_pResult) const
{
	return R_SUCCESS;
}


RC_NAMESPACE_END