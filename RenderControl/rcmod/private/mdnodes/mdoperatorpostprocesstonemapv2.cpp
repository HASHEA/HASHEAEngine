#include "pch.h"


#include "mdoperatorpostprocesstonemapv2.h"
#include <rc/public/iexecuter.h>

#ifdef RC_PERF
#include "KG3D_FrameTimer.h"
#else
#define FRAMETIMER_BEGIN(a,b)
#define FRAMETIMER_END(a)
#endif


RC_NAMESPACE_BEGIN

// MDSceneRender($F4)
static const unsigned int nParamCount = 12;
MDOperatorPostProcessTonemapV2::MDOperatorPostProcessTonemapV2()
    : m_OperatorName("MDPostProcessTonemapV2")
{

}

MDOperatorPostProcessTonemapV2::~MDOperatorPostProcessTonemapV2() 
{

}

/****************************
    FROM MDOperator class
****************************/
const DString&      MDOperatorPostProcessTonemapV2::getOperatorName() const
{
    return m_OperatorName;
}

Bool                MDOperatorPostProcessTonemapV2::isGpuOperator() const
{
    return DM_FALSE;
}

Int                 MDOperatorPostProcessTonemapV2::getInputCount() const
{
    return nParamCount;
}

DSimpleTypeID       MDOperatorPostProcessTonemapV2::getInputTypeID(Int i_Index) const
{
	DOME_ASSERT(i_Index < 0 || i_Index >= nParamCount);
	DSimpleTypeID InputTypes[nParamCount] = {
		RCGlobal::k_SimpleTypeID_OSTexture2D,		
		RCGlobal::k_SimpleTypeID_OSTexture2D,
		RCGlobal::k_SimpleTypeID_OSTexture2D,	
		RCGlobal::k_SimpleTypeID_OSTexture2D,
		RCGlobal::k_SimpleTypeID_OSTexture2D,
		RCGlobal::k_SimpleTypeID_DMatrix4x4f,
		RCGlobal::k_SimpleTypeID_DMatrix4x4f,
		RCGlobal::k_SimpleTypeID_DMatrix4x4f,
		RCGlobal::k_SimpleTypeID_DMatrix4x4f,
		RCGlobal::k_SimpleTypeID_DMatrix4x4f,
		RCGlobal::k_SimpleTypeID_DMatrix4x4f,
		RCGlobal::k_SimpleTypeID_DMatrix4x4f,
	};
	return InputTypes[i_Index];
}

Int                 MDOperatorPostProcessTonemapV2::getOutputCount() const
{
    return 1;
}

DSimpleTypeID       MDOperatorPostProcessTonemapV2::getOutputTypeID(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_ParamCount == nParamCount);
    return RCGlobal::k_SimpleTypeID_OSTexture2D;
}

RCGPUDATAFORMAT     MDOperatorPostProcessTonemapV2::getOutputTexFmt(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
	DOME_ASSERT(i_ParamCount == nParamCount);
	DOME_ASSERT(i_pParamList[0] && i_pParamList[0]->isTexture());

	return RGDF_RGBA16F;
}

DResult             MDOperatorPostProcessTonemapV2::calcOutputTexSize(Int i_Index, MDEffect* i_pMDEffect, DVector2i& o_Size, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_ParamCount == nParamCount);
    DOME_ASSERT(i_pParamList[0]	&& i_pParamList[0]->isTexture());


	DVector2i l_DepthBufferSize;

	i_pParamList[0]->getTextureSize(l_DepthBufferSize);

	o_Size = l_DepthBufferSize;

    return R_SUCCESS;
}

/****************************
    FROM MDOperatorCpu class
****************************/
MDOperandPtr        MDOperatorPostProcessTonemapV2::execute(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, IRP* o_pInputReleasePoint) const
{
	FRAMETIMER_BEGIN(FTT_RC_CAL_SCENERENDERTO, FTT_RC_CAL_EXECUTE_EFFECTPASS);
	DOME_ASSERT(i_ParamCount == nParamCount);
	DOME_ASSERT(i_pParamList[0] && i_pParamList[0]->isTexture());
	DOME_ASSERT(i_pParamList[1] && i_pParamList[1]->isTexture());
	DOME_ASSERT(i_pParamList[2] && i_pParamList[2]->isTexture());
	DOME_ASSERT(i_pParamList[3] && i_pParamList[3]->isTexture());
	DOME_ASSERT(i_pParamList[4] && i_pParamList[4]->isTexture());
	DOME_ASSERT(i_pParamList[5] && i_pParamList[5]->isMatrix4x4());
	DOME_ASSERT(i_pParamList[6] && i_pParamList[6]->isMatrix4x4());
	DOME_ASSERT(i_pParamList[7] && i_pParamList[7]->isMatrix4x4());
	DOME_ASSERT(i_pParamList[8] && i_pParamList[8]->isMatrix4x4());
	DOME_ASSERT(i_pParamList[9] && i_pParamList[9]->isMatrix4x4());
	DOME_ASSERT(i_pParamList[10] && i_pParamList[10]->isMatrix4x4());
	DOME_ASSERT(i_pParamList[11] && i_pParamList[11]->isMatrix4x4());
	
	RCEffectManager* l_pRCEffectMgr = i_pMDEffect->getRCEffect()->getEffectManager();
	RCRenderer* l_pRenderer = l_pRCEffectMgr->getRenderer();

	o_pInputReleasePoint[0] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[1] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[2] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[3] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[4] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[5] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[6] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[7] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[8] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[9] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[10] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[11] = IRP_AFTEREXECUTE;
	
	DSimpleTypedValue* l_pColorValue		= i_pParamList[0]->getDataPtr();
	DSimpleTypedValue* l_pExposureTexValue	= i_pParamList[1]->getDataPtr();
	DSimpleTypedValue* l_pCharacterExposureTexValue = i_pParamList[2]->getDataPtr();
	DSimpleTypedValue* l_pGBuffer0			= i_pParamList[3]->getDataPtr();
	DSimpleTypedValue* l_pDepth				= i_pParamList[4]->getDataPtr();
	DSimpleTypedValue* l_pmatParam0			= i_pParamList[5]->getDataPtr();
	DSimpleTypedValue* l_pmatParam1			= i_pParamList[6]->getDataPtr();
	DSimpleTypedValue* l_pmatParam2			= i_pParamList[7]->getDataPtr();
	DSimpleTypedValue* l_pmatParam3			= i_pParamList[8]->getDataPtr();
	DSimpleTypedValue* l_pmatParam4			= i_pParamList[9]->getDataPtr();
	DSimpleTypedValue* l_pmatParam5			= i_pParamList[10]->getDataPtr();
	DSimpleTypedValue* l_pmatParam6			= i_pParamList[11]->getDataPtr();

	DResult l_Result;

	DVector2i l_RtSize;
	calcOutputTexSize(0, i_pMDEffect, l_RtSize, i_ParamCount, i_pParamList);

	OSTexture2D l_RtTex;
	l_Result = l_pRenderer->createTexture2D(l_RtTex, l_RtSize.x, l_RtSize.y, 1, RGDF_RGBA8, RBU_DEFAULT, DM_TRUE, DM_NULL);
	DOME_ASSERT(DM_SUCC(l_Result));

	RCMOD_Texture l_ColorRtTexture;
	*((OSTexture2D*)l_ColorRtTexture.getPtr()) = l_RtTex;

	RCMOD_Texture l_ColorSrcTexture;
	*((OSTexture2D*)l_ColorSrcTexture.getPtr()) = l_pColorValue->getValue<OSTexture2D>();

	RCMOD_Texture l_ExposureTexture;
	*((OSTexture2D*)l_ExposureTexture.getPtr()) = l_pExposureTexValue->getValue<OSTexture2D>();

	RCMOD_Texture l_CharaterExposureTexture;
	*((OSTexture2D*)l_CharaterExposureTexture.getPtr()) = l_pCharacterExposureTexValue->getValue<OSTexture2D>();

	RCMOD_Texture l_GBuffer0Texture;
	*((OSTexture2D*)l_GBuffer0Texture.getPtr()) = l_pGBuffer0->getValue<OSTexture2D>();

	RCMOD_Texture l_DepthTexture;
	*((OSTexture2D*)l_DepthTexture.getPtr()) = l_pDepth->getValue<OSTexture2D>();

	RCMOD_Float4x4 l_Param0;
	*(DMatrix4x4f*)l_Param0.getPtr() = (l_pmatParam0->getDMatrix4x4f());

	RCMOD_Float4x4 l_Param1;
	*(DMatrix4x4f*)l_Param1.getPtr() = (l_pmatParam1->getDMatrix4x4f());

	RCMOD_Float4x4 l_Param2;
	*(DMatrix4x4f*)l_Param2.getPtr() = (l_pmatParam2->getDMatrix4x4f());

	RCMOD_Float4x4 l_Param3;
	*(DMatrix4x4f*)l_Param3.getPtr() = (l_pmatParam3->getDMatrix4x4f());

	RCMOD_Float4x4 l_Param4;
	*(DMatrix4x4f*)l_Param4.getPtr() = (l_pmatParam4->getDMatrix4x4f());

	RCMOD_Float4x4 l_Param5;
	*(DMatrix4x4f*)l_Param5.getPtr() = (l_pmatParam5->getDMatrix4x4f());

	RCMOD_Float4x4 l_Param6;
	*(DMatrix4x4f*)l_Param6.getPtr() = (l_pmatParam6->getDMatrix4x4f());

	// render
	const static DStringHash k_KEY_ScenePlugin("RCPI_Scene");
	RCPI_Scene* l_pScenePlugin = (RCPI_Scene*)l_pRCEffectMgr->getPlugin(k_KEY_ScenePlugin);


	l_pScenePlugin->RenderPostProcessTonemapV2((RCOSRendererData*)l_pRenderer->getOSRendererData(), &l_ColorRtTexture,
		&l_ColorSrcTexture, &l_ExposureTexture, &l_CharaterExposureTexture, &l_GBuffer0Texture, &l_DepthTexture, l_Param0, l_Param1, l_Param2, 
		l_Param3, l_Param4, l_Param5, l_Param6);


	MDOperandValue* l_pRtOperand = DOME_New(MDOperandValue)(RCGlobal::k_SimpleTypeID_OSTexture2D);
	l_pRtOperand->getDataPtr()->set(RCGlobal::k_SimpleTypeID_OSTexture2D, &l_RtTex);

	FRAMETIMER_END(FTT_RC_CAL_SCENERENDERTO);

	return l_pRtOperand;
}

DResult             MDOperatorPostProcessTonemapV2::destroyResult(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, MDOperandPtr i_pResult) const
{
	RCRenderer* l_pRenderer = i_pMDEffect->getRCEffect()->getEffectManager()->getRenderer();

	OSTexture2D l_hTexture;
	l_hTexture = *i_pResult->getTexturePtr();
	l_pRenderer->destroyTexture2D(l_hTexture);
	DOME_Del(i_pResult);

	return R_SUCCESS;
}


RC_NAMESPACE_END