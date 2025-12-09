#include "pch.h"
/*
    filename:       mdoperatorscenerender.cpp
    author:         Ming Dong
    date:           2016-JUN-28
    description:
*/

#include "mdoperatoratmospherevolumelight.h"
#include <rc/public/iexecuter.h>

#ifdef RC_PERF
#include "KG3D_FrameTimer.h"
#else
#define FRAMETIMER_BEGIN(a, b)
#define FRAMETIMER_END(a)
#endif


RC_NAMESPACE_BEGIN

// MDSceneRender($F4)
static const unsigned int nParamCount = 5; // 3 + 2
MDOperatorAtmosphereVolumeLight::MDOperatorAtmosphereVolumeLight()
	: m_OperatorName("MDAtmosphereVolumeLight")
{
}

MDOperatorAtmosphereVolumeLight::~MDOperatorAtmosphereVolumeLight()
{
}

/****************************
    FROM MDOperator class
****************************/
const DString& MDOperatorAtmosphereVolumeLight::getOperatorName() const
{
	return m_OperatorName;
}

Bool MDOperatorAtmosphereVolumeLight::isGpuOperator() const
{
	return DM_FALSE;
}

Int MDOperatorAtmosphereVolumeLight::getInputCount() const
{
	return nParamCount;
}

DSimpleTypeID MDOperatorAtmosphereVolumeLight::getInputTypeID(Int i_Index) const
{
	DOME_ASSERT(i_Index < 0 || i_Index >= nParamCount);
	DSimpleTypeID InputTypes[nParamCount] = {
		RCGlobal::k_SimpleTypeID_OSTexture2D, // Color Input
		RCGlobal::k_SimpleTypeID_OSTexture2D, // Depth Input
		RCGlobal::k_SimpleTypeID_OSTexture2D, // Cloud Depth Input
		RCGlobal::k_SimpleTypeID_DMatrix4x4f, // 0)
		RCGlobal::k_SimpleTypeID_DMatrix4x4f, // 0)
	};
	return InputTypes[i_Index];
}

Int MDOperatorAtmosphereVolumeLight::getOutputCount() const
{
	return 1;
}

DSimpleTypeID MDOperatorAtmosphereVolumeLight::getOutputTypeID(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
	DOME_ASSERT(i_ParamCount == nParamCount);
	return RCGlobal::k_SimpleTypeID_OSTexture2D;
}

RCGPUDATAFORMAT MDOperatorAtmosphereVolumeLight::getOutputTexFmt(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
	DOME_ASSERT(i_ParamCount == nParamCount);

	return RGDF_RGBA16F;
}

DResult MDOperatorAtmosphereVolumeLight::calcOutputTexSize(Int i_Index, MDEffect* i_pMDEffect, DVector2i& o_Size, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
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
MDOperandPtr MDOperatorAtmosphereVolumeLight::execute(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, IRP* o_pInputReleasePoint) const
{
	FRAMETIMER_BEGIN(FTT_RC_CAL_VOLUME_CLOUD, FTT_RC_CAL_EXECUTE_EFFECTPASS);
	DOME_ASSERT(i_ParamCount == nParamCount);
	DOME_ASSERT(i_pParamList[0] && i_pParamList[0]->isTexture());
	DOME_ASSERT(i_pParamList[1] && i_pParamList[1]->isTexture());
	DOME_ASSERT(i_pParamList[2] && i_pParamList[2]->isTexture());
	DOME_ASSERT(i_pParamList[3] && i_pParamList[3]->isMatrix4x4());
	DOME_ASSERT(i_pParamList[4] && i_pParamList[4]->isMatrix4x4());

	DResult l_Result;

	RCEffectManager* l_pRCEffectMgr = i_pMDEffect->getRCEffect()->getEffectManager();
	RCRenderer*      l_pRenderer    = l_pRCEffectMgr->getRenderer();

	o_pInputReleasePoint[0] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[1] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[2] = IRP_AFTEREXECUTE;

	DVector2i l_RtSize;
	l_Result = calcOutputTexSize(0, i_pMDEffect, l_RtSize, i_ParamCount, i_pParamList);
	DOME_ASSERT(DM_SUCC(l_Result));

	RCGPUDATAFORMAT l_Format = getOutputTexFmt(0, i_pMDEffect, i_ParamCount, i_pParamList);
	DOME_ASSERT(l_Format == RGDF_RGBA8 || l_Format == RGDF_RGBA16F);

	OSTexture2D l_RtTex;
	l_Result = l_pRenderer->createTexture2D(l_RtTex, l_RtSize.x, l_RtSize.y, 1, l_Format, RBU_DEFAULT, DM_TRUE, DM_NULL);
	DOME_ASSERT(DM_SUCC(l_Result));

	DVector4f f4clearColor = {0.0f, 0.0f, 0.0f, 0.0f};
	l_pRenderer->clearRenderTarget(l_RtTex, f4clearColor);

	RCViewportInfo viewport;
	viewport.m_TopLeftX = 0;
	viewport.m_TopLeftY = 0;
	viewport.m_Width    = l_RtSize.x;
	viewport.m_Height   = l_RtSize.y;
	viewport.m_MinDepth = 0;
	viewport.m_MaxDepth = 1;
	l_pRenderer->setViewports(1, &viewport);

	DSimpleTypedValue* l_pColorValue      = i_pParamList[0]->getDataPtr();
	DSimpleTypedValue* l_pDepthValue      = i_pParamList[1]->getDataPtr();
	DSimpleTypedValue* l_pCloudDepthValue = i_pParamList[2]->getDataPtr();
	DSimpleTypedValue* l_pmatParam0       = i_pParamList[3]->getDataPtr();
	DSimpleTypedValue* l_pmatParam1       = i_pParamList[4]->getDataPtr();

	RCMOD_Texture l_ColorSrcTexture;
	*((OSTexture2D*)l_ColorSrcTexture.getPtr()) = l_pColorValue->getValue<OSTexture2D>();

	RCMOD_Texture l_DepthTexture;
	*((OSTexture2D*)l_DepthTexture.getPtr()) = l_pDepthValue->getValue<OSTexture2D>();

	RCMOD_Texture l_CloudDepthTexture;
	*((OSTexture2D*)l_CloudDepthTexture.getPtr()) = l_pCloudDepthValue->getValue<OSTexture2D>();

	RCMOD_Texture l_ColorOutputTexture;
	*((OSTexture2D*)l_ColorOutputTexture.getPtr()) = l_RtTex;

	RCMOD_Float4x4 l_Param0;
	*(DMatrix4x4f*)l_Param0.getPtr() = (l_pmatParam0->getDMatrix4x4f());

	RCMOD_Float4x4 l_Param1;
	*(DMatrix4x4f*)l_Param1.getPtr() = (l_pmatParam1->getDMatrix4x4f());

	// render
	const static DStringHash k_KEY_ScenePlugin("RCPI_Scene");
	RCPI_Scene*              l_pScenePlugin = (RCPI_Scene*)l_pRCEffectMgr->getPlugin(k_KEY_ScenePlugin);

	l_pScenePlugin->RenderAtmosphereVolumeLight(
		(RCOSRendererData*)l_pRenderer->getOSRendererData(),
		&l_ColorOutputTexture,
		&l_ColorSrcTexture,
		&l_DepthTexture,
		&l_CloudDepthTexture,
		l_Param0,
		l_Param1
	);

	MDOperandValue* l_pRtOperand = DOME_New(MDOperandValue)(RCGlobal::k_SimpleTypeID_OSTexture2D);
	l_Result                     = l_pRtOperand->getDataPtr()->set(RCGlobal::k_SimpleTypeID_OSTexture2D, &l_RtTex);
	DOME_ASSERT(DM_SUCC(l_Result));

	FRAMETIMER_END(FTT_RC_CAL_SSSDEFERD);

	return l_pRtOperand;
}

DResult MDOperatorAtmosphereVolumeLight::destroyResult(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, MDOperandPtr i_pResult) const
{
	RCRenderer* l_pRenderer = i_pMDEffect->getRCEffect()->getEffectManager()->getRenderer();

	OSTexture2D l_hTexture;
	l_hTexture = *i_pResult->getTexturePtr();
	l_pRenderer->destroyTexture2D(l_hTexture);
	DOME_Del(i_pResult);

	return R_SUCCESS;
}


RC_NAMESPACE_END
