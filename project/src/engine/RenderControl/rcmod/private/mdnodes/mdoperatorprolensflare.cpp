#include "pch.h"
/*
filename:       mdoperatorscenerender.cpp
author:         Ming Dong
date:           2016-JUN-28
description:
*/

#include "MDOperatorProLensFlare.h"
#include <rc/public/iexecuter.h>

#ifdef RC_PERF
#include "KG3D_FrameTimer.h"
#else
#define FRAMETIMER_BEGIN(a,b)
#define FRAMETIMER_END(a)
#endif


RC_NAMESPACE_BEGIN

// MDSceneRender($F4)
static const unsigned int nParamCount = 7;
MDOperatorProLensFlare::MDOperatorProLensFlare()
	: m_OperatorName("MDProLensFlare")
{

}

MDOperatorProLensFlare::~MDOperatorProLensFlare()
{

}

/****************************
FROM MDOperator class
****************************/
const DString&      MDOperatorProLensFlare::getOperatorName() const
{
	return m_OperatorName;
}

Bool                MDOperatorProLensFlare::isGpuOperator() const
{
	return DM_FALSE;
}

Int                 MDOperatorProLensFlare::getInputCount() const
{
	return nParamCount;
}

DSimpleTypeID       MDOperatorProLensFlare::getInputTypeID(Int i_Index) const
{
	DOME_ASSERT(i_Index < 0 || i_Index >= nParamCount);
	DSimpleTypeID InputTypes[nParamCount] = {
		RCGlobal::k_SimpleTypeID_OSTexture2D,		//Color		Output
		RCGlobal::k_SimpleTypeID_OSTexture2D,		//Color		Input
		RCGlobal::k_SimpleTypeID_OSTexture2D,		//Depth		Input
		RCGlobal::k_SimpleTypeID_DVector3f,			//Dir		Input
		RCGlobal::k_SimpleTypeID_DString,		// 0)
		RCGlobal::k_SimpleTypeID_DVector4f,
		RCGlobal::k_SimpleTypeID_DVector4f
	};
	return InputTypes[i_Index];
}

Int                 MDOperatorProLensFlare::getOutputCount() const
{
	return 1;
}

DSimpleTypeID       MDOperatorProLensFlare::getOutputTypeID(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
	DOME_ASSERT(i_ParamCount == nParamCount);
	return RCGlobal::k_SimpleTypeID_OSTexture2D;
}

RCGPUDATAFORMAT     MDOperatorProLensFlare::getOutputTexFmt(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
	DOME_ASSERT(i_ParamCount == nParamCount);

	return RGDF_RGBA16F;
}

DResult             MDOperatorProLensFlare::calcOutputTexSize(Int i_Index, MDEffect* i_pMDEffect, DVector2i& o_Size, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
	DOME_ASSERT(i_ParamCount == nParamCount);
	DOME_ASSERT(i_pParamList[0] && i_pParamList[0]->isTexture());

	DVector2i l_ColorBufferSize;
	i_pParamList[0]->getTextureSize(l_ColorBufferSize);

	o_Size = l_ColorBufferSize;
	return R_SUCCESS;
}

/****************************
FROM MDOperatorCpu class
****************************/
MDOperandPtr        MDOperatorProLensFlare::execute(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, IRP* o_pInputReleasePoint) const
{
	FRAMETIMER_BEGIN(FTT_RC_CAL_VOLUME_CLOUD, FTT_RC_CAL_EXECUTE_EFFECTPASS);
	DOME_ASSERT(i_ParamCount == nParamCount);
	DOME_ASSERT(i_pParamList[0] && i_pParamList[0]->isTexture());
	DOME_ASSERT(i_pParamList[1] && i_pParamList[1]->isTexture());
	DOME_ASSERT(i_pParamList[2] && i_pParamList[2]->isTexture());
	DOME_ASSERT(i_pParamList[3] && i_pParamList[3]->isFloat3());
	DOME_ASSERT(i_pParamList[5] && i_pParamList[5]->isFloat4());
	DOME_ASSERT(i_pParamList[6] && i_pParamList[6]->isFloat4());

	DResult l_Result;

	RCEffectManager* l_pRCEffectMgr = i_pMDEffect->getRCEffect()->getEffectManager();
	RCRenderer* l_pRenderer = l_pRCEffectMgr->getRenderer();

	o_pInputReleasePoint[0] = IRP_INFINISHCALLBACK;
	o_pInputReleasePoint[1] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[2] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[3] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[4] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[5] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[6] = IRP_AFTEREXECUTE;

	DSimpleTypedValue* l_pColorOutputValue = i_pParamList[0]->getDataPtr();
	DSimpleTypedValue* l_pColorInputValue = i_pParamList[1]->getDataPtr();
	DSimpleTypedValue* l_pDepthInputValue = i_pParamList[2]->getDataPtr();
	DSimpleTypedValue* l_pLightDirValue = i_pParamList[3]->getDataPtr();
	DSimpleTypedValue* l_FilePathValue = i_pParamList[4]->getDataPtr();
	DSimpleTypedValue* l_ParamValue = i_pParamList[5]->getDataPtr();
	DSimpleTypedValue* l_GlobalTintColorValue = i_pParamList[6]->getDataPtr();

	DOME_ASSERT(l_FilePathValue && l_FilePathValue->getTypeID() == RCGlobal::k_SimpleTypeID_DString);

	RCMOD_Texture l_ColorOutputTexture;
	*((OSTexture2D*)l_ColorOutputTexture.getPtr()) = l_pColorOutputValue->getValue<OSTexture2D>();

	RCMOD_Texture l_ColorSrcTexture;
	*((OSTexture2D*)l_ColorSrcTexture.getPtr()) = l_pColorInputValue->getValue<OSTexture2D>();

	RCMOD_Texture l_DepthTexture;
	*((OSTexture2D*)l_DepthTexture.getPtr()) = l_pDepthInputValue->getValue<OSTexture2D>();

	RCMOD_Float3 l_LightDir;
	*(DVector3f*)l_LightDir.getPtr() = (l_pLightDirValue->getDVector3f());

	DString l_FilePath = l_FilePathValue->getDString();
	RCMOD_String l_rcms_FilePath(l_FilePath.c_str());

	RCMOD_Float4 l_v4Param;
	*(DVector4f*)l_v4Param.getPtr() = (l_ParamValue->getDVector4f());

	RCMOD_Float4 l_GlobalTintColor;
	*(DVector4f*)l_GlobalTintColor.getPtr() = (l_GlobalTintColorValue->getDVector4f());

	// render
	const static DStringHash k_KEY_ScenePlugin("RCPI_Scene");
	RCPI_Scene* l_pScenePlugin = (RCPI_Scene*)l_pRCEffectMgr->getPlugin(k_KEY_ScenePlugin);

	l_pScenePlugin->RenderProLens(
		(RCOSRendererData*)l_pRenderer->getOSRendererData(),
		&l_ColorOutputTexture,
		&l_ColorSrcTexture,
		&l_DepthTexture,
		l_LightDir,
		l_rcms_FilePath,
		l_v4Param,
		l_GlobalTintColor
	);

	MDOperandValue* l_pRtOperand = DOME_New(MDOperandValue)(RCGlobal::k_SimpleTypeID_OSTexture2D);
	l_Result = l_pRtOperand->getDataPtr()->set(RCGlobal::k_SimpleTypeID_OSTexture2D, l_ColorOutputTexture.getPtr());
	DOME_ASSERT(DM_SUCC(l_Result));

	FRAMETIMER_END(FTT_RC_CAL_VOLUME_CLOUD);

	return l_pRtOperand;
}

DResult             MDOperatorProLensFlare::destroyResult(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, MDOperandPtr i_pResult) const
{
    DOME_Del(i_pResult);
	return R_SUCCESS;
}


RC_NAMESPACE_END