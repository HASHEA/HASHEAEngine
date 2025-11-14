#include "pch.h"
/*
    filename:       mdoperatorscenerender.cpp
    author:         Ming Dong
    date:           2016-JUN-28
    description:    
*/

#include "mdoperatorbrunetonsimprovedatmosphere.h"
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
MDOperatorBrunetonsImprovedAtmosphere::MDOperatorBrunetonsImprovedAtmosphere()
    : m_OperatorName("MDBrunetonsImprovedAtmosphere")
{

}

MDOperatorBrunetonsImprovedAtmosphere::~MDOperatorBrunetonsImprovedAtmosphere() 
{

}

/****************************
    FROM MDOperator class
****************************/
const DString&      MDOperatorBrunetonsImprovedAtmosphere::getOperatorName() const
{
    return m_OperatorName;
}

Bool                MDOperatorBrunetonsImprovedAtmosphere::isGpuOperator() const
{
    return DM_FALSE;
}

Int                 MDOperatorBrunetonsImprovedAtmosphere::getInputCount() const
{
    return nParamCount;
}

DSimpleTypeID       MDOperatorBrunetonsImprovedAtmosphere::getInputTypeID(Int i_Index) const
{
	DOME_ASSERT(i_Index < 0 || i_Index >= nParamCount);
	DSimpleTypeID InputTypes[nParamCount] = {
		RCGlobal::k_SimpleTypeID_OSTexture2D,		//Color		Output
		RCGlobal::k_SimpleTypeID_OSTexture2D,		//Color		Input
		RCGlobal::k_SimpleTypeID_OSTexture2D,		//Depth		Input
		RCGlobal::k_SimpleTypeID_OSTexture2D,		//GBufferD	Input
		RCGlobal::k_SimpleTypeID_DVector3f,			//LightDir
		RCGlobal::k_SimpleTypeID_DVector3f,			//LightDir
		RCGlobal::k_SimpleTypeID_DMatrix4x4f		// 0)
	};
	return InputTypes[i_Index];
}

Int                 MDOperatorBrunetonsImprovedAtmosphere::getOutputCount() const
{
    return 1;
}

DSimpleTypeID       MDOperatorBrunetonsImprovedAtmosphere::getOutputTypeID(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_ParamCount == nParamCount);
    return RCGlobal::k_SimpleTypeID_OSTexture2D;
}

RCGPUDATAFORMAT     MDOperatorBrunetonsImprovedAtmosphere::getOutputTexFmt(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
	DOME_ASSERT(i_ParamCount == nParamCount);

	return RGDF_RGBA16F;
}

DResult             MDOperatorBrunetonsImprovedAtmosphere::calcOutputTexSize(Int i_Index, MDEffect* i_pMDEffect, DVector2i& o_Size, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_ParamCount == nParamCount);
    DOME_ASSERT(i_pParamList[0]	&& i_pParamList[0]->isTexture());	

	DVector2i l_ColorBufferSize;	
	i_pParamList[0]->getTextureSize(l_ColorBufferSize);

	o_Size = l_ColorBufferSize;
    return R_SUCCESS;
}

/****************************
    FROM MDOperatorCpu class
****************************/
MDOperandPtr        MDOperatorBrunetonsImprovedAtmosphere::execute(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, IRP* o_pInputReleasePoint) const
{
    PERF_COUNTER_EX(0);
	FRAMETIMER_BEGIN(FTT_RC_CAL_VOLUME_CLOUD, FTT_RC_CAL_EXECUTE_EFFECTPASS);
	DOME_ASSERT(i_ParamCount == nParamCount);
	DOME_ASSERT(i_pParamList[0] && i_pParamList[0]->isTexture());
	DOME_ASSERT(i_pParamList[1] && i_pParamList[1]->isTexture());
	DOME_ASSERT(i_pParamList[2] && i_pParamList[3]->isTexture());
	DOME_ASSERT(i_pParamList[3] && i_pParamList[3]->isTexture());
	DOME_ASSERT(i_pParamList[4] && i_pParamList[4]->isFloat3());
	DOME_ASSERT(i_pParamList[5] && i_pParamList[5]->isFloat3());
	DOME_ASSERT(i_pParamList[6] && i_pParamList[6]->isMatrix4x4());
	
//	DResult l_Result;
    
    RCEffectManager* l_pRCEffectMgr = i_pMDEffect->getRCEffect()->getEffectManager();
    RCRenderer* l_pRenderer = l_pRCEffectMgr->getRenderer();

	o_pInputReleasePoint[0] = IRP_INFINISHCALLBACK;
	o_pInputReleasePoint[1] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[2] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[3] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[4] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[5] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[6] = IRP_AFTEREXECUTE;

	DSimpleTypedValue* l_pColorOutputValue				= i_pParamList[0]->getDataPtr();
    DSimpleTypedValue* l_pColorInputValue				= i_pParamList[1]->getDataPtr();
	DSimpleTypedValue* l_pDepthInputValue				= i_pParamList[2]->getDataPtr();	
	DSimpleTypedValue* l_pGBufferAInputValue			= i_pParamList[3]->getDataPtr();
	DSimpleTypedValue* l_pf3SunDir						= i_pParamList[4]->getDataPtr();
	DSimpleTypedValue* l_pf3SkyDir						= i_pParamList[5]->getDataPtr();
	DSimpleTypedValue* l_pmatParam0						= i_pParamList[6]->getDataPtr();
	
	RCMOD_Texture l_ColorOutputTexture;
	*((OSTexture2D*)l_ColorOutputTexture.getPtr()) = l_pColorOutputValue->getValue<OSTexture2D>();

	RCMOD_Texture l_ColorSrcTexture;
	*((OSTexture2D*)l_ColorSrcTexture.getPtr()) = l_pColorInputValue->getValue<OSTexture2D>();

	RCMOD_Texture l_DepthTexture;
	*((OSTexture2D*)l_DepthTexture.getPtr()) = l_pDepthInputValue->getValue<OSTexture2D>();

	RCMOD_Texture l_GBufferATexture;
	*((OSTexture2D*)l_GBufferATexture.getPtr()) = l_pGBufferAInputValue->getValue<OSTexture2D>();

	RCMOD_Float3 l_SunDir;
	*(DVector3f*)l_SunDir.getPtr() = (l_pf3SunDir->getDVector3f());

	RCMOD_Float3 l_SkyDir;
	*(DVector3f*)l_SkyDir.getPtr() = (l_pf3SkyDir->getDVector3f());

	RCMOD_Float4x4 l_Param0;
	*(DMatrix4x4f*)l_Param0.getPtr() = (l_pmatParam0->getDMatrix4x4f());

    // render
    const static DStringHash k_KEY_ScenePlugin("RCPI_Scene");
    RCPI_Scene* l_pScenePlugin = (RCPI_Scene*)l_pRCEffectMgr->getPlugin(k_KEY_ScenePlugin);
	
	l_pScenePlugin->RenderBrunetonsImprovedAtmosphere(
		(RCOSRendererData*)l_pRenderer->getOSRendererData(), 
		&l_ColorOutputTexture,		
		&l_ColorSrcTexture, 
		&l_DepthTexture,
		&l_GBufferATexture,
		l_SunDir,
		l_SkyDir,
		l_Param0
		);

	return i_pParamList[0];
}

DResult             MDOperatorBrunetonsImprovedAtmosphere::destroyResult(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, MDOperandPtr i_pResult) const
{
	return R_SUCCESS;
}


RC_NAMESPACE_END