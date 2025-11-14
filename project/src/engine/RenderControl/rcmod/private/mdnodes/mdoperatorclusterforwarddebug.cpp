#include "pch.h"
/*
	filename:       mdoperatorscenerender.hMDOperatorClusterForwardDebug
	author:         Mingrui Liu
	date:           2022-DEC-22
	description:
*/

#include "mdoperatorclusterforwarddebug.h"
#include <rc/public/iexecuter.h>
#include "../../../DevEnv/Include/PerfAnalyzer.h"

#ifdef RC_PERF
#include "KG3D_FrameTimer.h"
#else
#define FRAMETIMER_BEGIN(a,b)
#define FRAMETIMER_END(a)
#endif


RC_NAMESPACE_BEGIN

static unsigned int nParamCount = 3;
MDOperatorClusterForwardDebug::MDOperatorClusterForwardDebug()
	: m_OperatorName("MDClusterForwardDebug")
{
	
}

MDOperatorClusterForwardDebug::~MDOperatorClusterForwardDebug()
{

}

/****************************
	FROM MDOperator class
****************************/
const DString& MDOperatorClusterForwardDebug::getOperatorName() const
{
	return m_OperatorName;
}

Bool                MDOperatorClusterForwardDebug::isGpuOperator() const
{
	return DM_FALSE;
}

Int                 MDOperatorClusterForwardDebug::getInputCount() const
{
	return nParamCount;
}

DSimpleTypeID       MDOperatorClusterForwardDebug::getInputTypeID(Int i_Index) const
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

Int      MDOperatorClusterForwardDebug::getOutputCount() const
{
	return 1;
}

DSimpleTypeID       MDOperatorClusterForwardDebug::getOutputTypeID(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
	DOME_ASSERT(i_ParamCount == nParamCount);

	return RCGlobal::k_SimpleTypeID_OSTexture2D;;
}

RCGPUDATAFORMAT     MDOperatorClusterForwardDebug::getOutputTexFmt(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
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

DResult             MDOperatorClusterForwardDebug::calcOutputTexSize(Int i_Index, MDEffect* i_pMDEffect, DVector2i& o_Size, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
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
MDOperandPtr        MDOperatorClusterForwardDebug::execute(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, IRP* o_pInputReleasePoint) const
{
	PERF_COUNTER_EX(0);
	FRAMETIMER_BEGIN(FTT_RC_CAL_CLUSTER_FORWARD_DEBUG, FTT_RC_CAL_EXECUTE_EFFECTPASS);
	DOME_ASSERT(i_ParamCount == nParamCount);
	DOME_ASSERT(i_pParamList[0] && i_pParamList[0]->isTexture());           //color
	DOME_ASSERT(i_pParamList[1] && i_pParamList[1]->isTexture());           //depth
	DOME_ASSERT(i_pParamList[2] && i_pParamList[2]->isFloat());             //debugType

	RCEffectManager* l_pRCEffectMgr = i_pMDEffect->getRCEffect()->getEffectManager();
	RCRenderer* l_pRenderer = l_pRCEffectMgr->getRenderer();

	o_pInputReleasePoint[0] = IRP_INFINISHCALLBACK;
	o_pInputReleasePoint[1] = IRP_INFINISHCALLBACK;
	o_pInputReleasePoint[2] = IRP_AFTEREXECUTE;

	DSimpleTypedValue* l_pColorSrcValue = i_pParamList[0]->getDataPtr();
	DSimpleTypedValue* l_pDepthValue = i_pParamList[1]->getDataPtr();

	RCMOD_Texture l_ColorSrcTexture;
	*((OSTexture2D*)l_ColorSrcTexture.getPtr()) = l_pColorSrcValue->getValue<OSTexture2D>();

	RCMOD_Texture l_DepthTexture;
	*((OSTexture2D*)l_DepthTexture.getPtr()) = l_pDepthValue->getValue<OSTexture2D>();

	float l_DebugType;
	i_pParamList[2]->getFloat(l_DebugType);

	// render
	const static DStringHash k_KEY_ScenePlugin("RCPI_Scene");
	RCPI_Scene* l_pScenePlugin = (RCPI_Scene*)l_pRCEffectMgr->getPlugin(k_KEY_ScenePlugin);

	l_pScenePlugin->RenderClusterForwardDebug((RCOSRendererData*)l_pRenderer->getOSRendererData(), &l_ColorSrcTexture, &l_DepthTexture, l_DebugType);

	FRAMETIMER_END(FTT_RC_CAL_CLUSTER_FORWARD_DEBUG);

	return i_pParamList[0];

}

DResult             MDOperatorClusterForwardDebug::destroyResult(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, MDOperandPtr i_pResult) const
{
	return R_SUCCESS;
}


RC_NAMESPACE_END