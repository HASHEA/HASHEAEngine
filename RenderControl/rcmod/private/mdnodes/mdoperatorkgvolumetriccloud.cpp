#include "pch.h"
/*
    filename:       mdoperatorscenerender.cpp
    author:         Ming Dong
    date:           2016-JUN-28
    description:    
*/

#include "mdoperatorkgvolumetriccloud.h"
#include <rc/public/iexecuter.h>

#ifdef RC_PERF
#include "KG3D_FrameTimer.h"
#else
#define FRAMETIMER_BEGIN(a,b)
#define FRAMETIMER_END(a)
#endif


RC_NAMESPACE_BEGIN

// MDSceneRender($F4)
static const unsigned int nParamCount = 9;
MDOperatorKGVolumetricCloud::MDOperatorKGVolumetricCloud()
    : m_OperatorName("MDKGVolumetricCloud")
{

}

MDOperatorKGVolumetricCloud::~MDOperatorKGVolumetricCloud() 
{

}

/****************************
    FROM MDOperator class
****************************/
const DString&      MDOperatorKGVolumetricCloud::getOperatorName() const
{
    return m_OperatorName;
}

Bool                MDOperatorKGVolumetricCloud::isGpuOperator() const
{
    return DM_FALSE;
}

Int                 MDOperatorKGVolumetricCloud::getInputCount() const
{
    return nParamCount;
}

DSimpleTypeID       MDOperatorKGVolumetricCloud::getInputTypeID(Int i_Index) const
{
	DOME_ASSERT(i_Index < 0 || i_Index >= nParamCount);
	DSimpleTypeID InputTypes[nParamCount] = {
		RCGlobal::k_SimpleTypeID_OSTexture2D,		//Depth Input
		RCGlobal::k_SimpleTypeID_OSTexture2D,		//Cirrus Tex
		RCGlobal::k_SimpleTypeID_OSTexture2D,		//Weather Tex
		RCGlobal::k_SimpleTypeID_DVector3f,			//LightDir
		
        RCGlobal::k_SimpleTypeID_DMatrix4x4f,		// 0)
		RCGlobal::k_SimpleTypeID_DMatrix4x4f,		// 0)
		RCGlobal::k_SimpleTypeID_DMatrix4x4f,		// 0)
		RCGlobal::k_SimpleTypeID_DMatrix4x4f,		// 0)
		RCGlobal::k_SimpleTypeID_DMatrix4x4f		// 0)
	};
	return InputTypes[i_Index];
}

Int                 MDOperatorKGVolumetricCloud::getOutputCount() const
{
    return 1;
}

DSimpleTypeID       MDOperatorKGVolumetricCloud::getOutputTypeID(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_ParamCount == nParamCount);
    return RCGlobal::k_SimpleTypeID_OSTexture2D;
}

RCGPUDATAFORMAT     MDOperatorKGVolumetricCloud::getOutputTexFmt(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
	DOME_ASSERT(i_ParamCount == nParamCount);
	DOME_ASSERT(i_pParamList[0] && i_pParamList[0]->isTexture());

	return RGDF_RGBA16F;
}

DResult             MDOperatorKGVolumetricCloud::calcOutputTexSize(Int i_Index, MDEffect* i_pMDEffect, DVector2i& o_Size, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
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
MDOperandPtr        MDOperatorKGVolumetricCloud::execute(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, IRP* o_pInputReleasePoint) const
{
	FRAMETIMER_BEGIN(FTT_RC_CAL_VOLUME_CLOUD, FTT_RC_CAL_EXECUTE_EFFECTPASS);
	DOME_ASSERT(i_ParamCount == nParamCount);
	DOME_ASSERT(i_pParamList[0 ] && i_pParamList[0 ]->isTexture());
	DOME_ASSERT(i_pParamList[1 ] && i_pParamList[1 ]->isTexture());
	DOME_ASSERT(i_pParamList[2 ] && i_pParamList[2 ]->isTexture());
	DOME_ASSERT(i_pParamList[3 ] && i_pParamList[3 ]->isFloat3());
	DOME_ASSERT(i_pParamList[4 ] && i_pParamList[4 ]->isMatrix4x4());
	DOME_ASSERT(i_pParamList[5 ] && i_pParamList[5 ]->isMatrix4x4());
	DOME_ASSERT(i_pParamList[6 ] && i_pParamList[6 ]->isMatrix4x4());
	DOME_ASSERT(i_pParamList[7 ] && i_pParamList[7 ]->isMatrix4x4());
	DOME_ASSERT(i_pParamList[8 ] && i_pParamList[8 ]->isMatrix4x4());
	
	DResult l_Result;
    
    RCEffectManager* l_pRCEffectMgr = i_pMDEffect->getRCEffect()->getEffectManager();
    RCRenderer* l_pRenderer = l_pRCEffectMgr->getRenderer();

	o_pInputReleasePoint[0 ] = IRP_INFINISHCALLBACK;
	o_pInputReleasePoint[1 ] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[2 ] = IRP_AFTEREXECUTE;
    o_pInputReleasePoint[3 ] = IRP_AFTEREXECUTE;
    o_pInputReleasePoint[4 ] = IRP_AFTEREXECUTE;
    o_pInputReleasePoint[5 ] = IRP_AFTEREXECUTE;
    o_pInputReleasePoint[6 ] = IRP_AFTEREXECUTE;
    o_pInputReleasePoint[7 ] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[8 ] = IRP_AFTEREXECUTE;

    DSimpleTypedValue* l_pDepthValue					= i_pParamList[0 ]->getDataPtr();
	DSimpleTypedValue* l_pCirrusTexValue	            = i_pParamList[1 ]->getDataPtr();
    DSimpleTypedValue* l_pWeatherData                   = i_pParamList[2 ]->getDataPtr();
	DSimpleTypedValue* l_pf3LightDir					= i_pParamList[3 ]->getDataPtr();
    DSimpleTypedValue* l_pmatParam0						= i_pParamList[4 ]->getDataPtr();
    DSimpleTypedValue* l_pmatParam1						= i_pParamList[5 ]->getDataPtr();
    DSimpleTypedValue* l_pmatParam2						= i_pParamList[6 ]->getDataPtr();
    DSimpleTypedValue* l_pmatParam3						= i_pParamList[7 ]->getDataPtr();
    DSimpleTypedValue* l_pmatParam4						= i_pParamList[8 ]->getDataPtr();
        

	// set viewport
	DVector2i l_RtSize;
	calcOutputTexSize(0, i_pMDEffect, l_RtSize, i_ParamCount, i_pParamList);

	RCMOD_Texture l_DepthTexture;
	*((OSTexture2D*)l_DepthTexture.getPtr()) = l_pDepthValue->getValue<OSTexture2D>();

	RCMOD_Texture l_Cirrus2DTexture;
	*((OSTexture2D*)l_Cirrus2DTexture.getPtr()) = l_pCirrusTexValue->getValue<OSTexture2D>();

	RCMOD_Texture l_Weather2DTexture;
	*((OSTexture2D*)l_Weather2DTexture.getPtr()) = l_pWeatherData->getValue<OSTexture2D>();

	RCMOD_Float3 l_LightDir;
	*(DVector3f*)l_LightDir.getPtr() = (l_pf3LightDir->getDVector3f());

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

	OSTexture2D l_RtTex;
	l_Result = l_pRenderer->createTexture2D(l_RtTex, l_RtSize.x, l_RtSize.y, 1, RGDF_RGBA16F, RBU_DEFAULT, DM_TRUE, DM_NULL);
	DOME_ASSERT(DM_SUCC(l_Result));

	/*
	// clear render target if necessary
	if (l_ClearColor.x >= 0.0f)
	{
		l_pRenderer->clearRenderTarget(l_RtTex, l_ClearColor);
	}

	// set viewport
	RCMOD_Float4 l_ViewPort(l_Viewport.x * l_RtSize.x, l_Viewport.y * l_RtSize.y, l_Viewport.z * l_RtSize.x, l_Viewport.w * l_RtSize.y);
	*/

	RCMOD_Texture l_ColorTexture;
	*((OSTexture2D*)l_ColorTexture.getPtr()) = l_RtTex;

    // render
    const static DStringHash k_KEY_ScenePlugin("RCPI_Scene");
    RCPI_Scene* l_pScenePlugin = (RCPI_Scene*)l_pRCEffectMgr->getPlugin(k_KEY_ScenePlugin);

	
	l_pScenePlugin->RenderVolumetricCloud(
		(RCOSRendererData*)l_pRenderer->getOSRendererData(), 
		&l_ColorTexture, 
		&l_DepthTexture,
		&l_Cirrus2DTexture,
		&l_Weather2DTexture,
		l_LightDir,
		l_Param0,
		l_Param1,
		l_Param2,
		l_Param3,
		l_Param4
		);

	MDOperandValue* l_pRtOperand = DOME_New(MDOperandValue)(RCGlobal::k_SimpleTypeID_OSTexture2D);
	l_Result = l_pRtOperand->getDataPtr()->set(RCGlobal::k_SimpleTypeID_OSTexture2D, &l_RtTex);
	DOME_ASSERT(DM_SUCC(l_Result));

	FRAMETIMER_END(FTT_RC_CAL_VOLUME_CLOUD);

	return l_pRtOperand;
}

DResult             MDOperatorKGVolumetricCloud::destroyResult(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, MDOperandPtr i_pResult) const
{
	RCRenderer* l_pRenderer = i_pMDEffect->getRCEffect()->getEffectManager()->getRenderer();

	OSTexture2D l_hTexture;
	l_hTexture = *i_pResult->getTexturePtr();
	l_pRenderer->destroyTexture2D(l_hTexture);
	DOME_Del(i_pResult);

	return R_SUCCESS;
}


RC_NAMESPACE_END