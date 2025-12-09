#include "pch.h"
/*
filename:       mdoperatorgivoxelizationdebug.cpp
author:         Bin Yang
date:           2018-Aug-14
description:
*/
#include "mdoperatordefaultvxgiwithouttrp.h"
#include <rc/public/iexecuter.h>
#include "mdoperatorgivoxelizationdebug.h"

RC_NAMESPACE_BEGIN

static unsigned int nParamCount = 3;

MDOperatorGIVoxelizationDebug::MDOperatorGIVoxelizationDebug()
	: m_OperatorName("MDGIVoxelizationDebug")
{
}

MDOperatorGIVoxelizationDebug::~MDOperatorGIVoxelizationDebug()
{
}

/****************************
FROM MDOperator class
****************************/
const DString& MDOperatorGIVoxelizationDebug::getOperatorName() const
{
	return m_OperatorName;
}

Bool MDOperatorGIVoxelizationDebug::isGpuOperator() const
{
	return DM_FALSE;
}

Int MDOperatorGIVoxelizationDebug::getInputCount() const
{
	return nParamCount;
}

DSimpleTypeID MDOperatorGIVoxelizationDebug::getInputTypeID(Int i_Index) const
{
	DSimpleTypeID l_ParamTypeID;

	switch (i_Index)
	{
	case 0:
	case 1:
		l_ParamTypeID = RCGlobal::k_SimpleTypeID_OSTexture2D;
		break;
	case 2:
		l_ParamTypeID = RCGlobal::k_SimpleTypeID_DMatrix4x4f;
		break;
	}

	return l_ParamTypeID;
}

Int MDOperatorGIVoxelizationDebug::getOutputCount() const
{
	return 1;
}

DSimpleTypeID MDOperatorGIVoxelizationDebug::getOutputTypeID(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
	DOME_ASSERT(i_ParamCount == nParamCount);
	return RCGlobal::k_SimpleTypeID_OSTexture2D;
}

RCGPUDATAFORMAT MDOperatorGIVoxelizationDebug::getOutputTexFmt(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
	DOME_ASSERT(i_ParamCount == nParamCount);
	DOME_ASSERT(i_pParamList[0] && i_pParamList[0]->isTexture());
	DOME_ASSERT(i_pParamList[1] && i_pParamList[1]->isTexture());
	DOME_ASSERT(i_pParamList[2] && i_pParamList[2]->isMatrix4x4());

	RCGPUDATAFORMAT l_Format;
	DResult l_Result;

	l_Result = i_pParamList[0]->getTextureFormat(l_Format);
	DOME_ASSERT(DM_SUCC(l_Result));
	return l_Format;
}

DResult MDOperatorGIVoxelizationDebug::calcOutputTexSize(Int i_Index, MDEffect* i_pMDEffect, DVector2i& o_Size, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
	DOME_ASSERT(i_ParamCount == nParamCount);
	DOME_ASSERT(i_pParamList[0] && i_pParamList[0]->isTexture());
	DOME_ASSERT(i_pParamList[1] && i_pParamList[1]->isTexture());
	DOME_ASSERT(i_pParamList[2] && i_pParamList[2]->isMatrix4x4());

	DResult l_Result;

	l_Result = i_pParamList[0]->getTextureSize(o_Size);
	DOME_ASSERT(DM_SUCC(l_Result));
	return R_SUCCESS;
}

/****************************
FROM MDOperatorCpu class
****************************/
MDOperandPtr MDOperatorGIVoxelizationDebug::execute(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, IRP* o_pInputReleasePoint) const
{
	DOME_ASSERT(i_ParamCount == nParamCount);
	DOME_ASSERT(i_pParamList[0] && i_pParamList[0]->isTexture());
	DOME_ASSERT(i_pParamList[1] && i_pParamList[1]->isTexture());
	DOME_ASSERT(i_pParamList[2] && i_pParamList[2]->isMatrix4x4());

//	DResult dResult;
	RCEffectManager* l_pEffectManager = i_pMDEffect->getRCEffect()->getEffectManager();
	RCRenderer* l_pRenderer = l_pEffectManager->getRenderer();

	o_pInputReleasePoint[0] = IRP_INFINISHCALLBACK;
	o_pInputReleasePoint[1] = IRP_INFINISHCALLBACK;
	o_pInputReleasePoint[2] = IRP_AFTEREXECUTE;

	DSimpleTypedValue* l_pColorTexture = i_pParamList[0]->getDataPtr();
	DSimpleTypedValue* l_pDepthTexture = i_pParamList[1]->getDataPtr();
	DSimpleTypedValue* l_pmatParam0 = i_pParamList[2]->getDataPtr();

	RCMOD_Texture l_rcTextureColor;
	*((OSTexture2D*)l_rcTextureColor.getPtr()) = l_pColorTexture->getValue<OSTexture2D>();
	RCMOD_Texture l_rcTextureDepth;
	*((OSTexture2D*)l_rcTextureDepth.getPtr()) = l_pDepthTexture->getValue<OSTexture2D>();

	RCMOD_Float4x4 l_Param0;
	*(DMatrix4x4f*)l_Param0.getPtr() = (l_pmatParam0->getDMatrix4x4f());

	const static DStringHash k_KEY_ScenePlugin("RCPI_Scene");
	RCPI_Scene* l_pScenePlugin = (RCPI_Scene*)l_pEffectManager->getPlugin(k_KEY_ScenePlugin);

	l_pScenePlugin->RenderGIVoxelization((RCOSRendererData*)l_pRenderer->getOSRendererData(),
		&l_rcTextureColor,
		&l_rcTextureDepth,
		l_Param0);

	return i_pParamList[0];
}

DResult MDOperatorGIVoxelizationDebug::destroyResult(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, MDOperandPtr i_pResult) const
{
	return R_SUCCESS;
}

RC_NAMESPACE_END