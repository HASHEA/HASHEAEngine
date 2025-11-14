#include "pch.h"
/*
    filename:       mdoperatorvolumetricheightfog.cpp
    author:         Huang Yida
    date:           2019-july-8
    description:    
*/

#include "mdoperatorVolumetricHeightFog.h"
#include <rc/public/iexecuter.h>

#ifdef RC_PERF
#include "KG3D_FrameTimer.h"
#else
#define FRAMETIMER_BEGIN(a,b)
#define FRAMETIMER_END(a)
#endif


RC_NAMESPACE_BEGIN
static const unsigned int nParamCount = 11;
MDOperatorVolumetricHeightFog::MDOperatorVolumetricHeightFog()
	: m_OperatorName("MDVolumetricHeightFog")
{

}

MDOperatorVolumetricHeightFog::~MDOperatorVolumetricHeightFog()
{

}

const DString&      MDOperatorVolumetricHeightFog::getOperatorName() const
{
	return m_OperatorName;
}

Bool                MDOperatorVolumetricHeightFog::isGpuOperator() const
{
	return DM_FALSE;
}

Int                 MDOperatorVolumetricHeightFog::getInputCount() const
{
	return nParamCount;
}

DSimpleTypeID       MDOperatorVolumetricHeightFog::getInputTypeID(Int i_Index) const
{
	DOME_ASSERT(i_Index < 0 || i_Index >= nParamCount);
	DSimpleTypeID InputTypes[nParamCount] = {
		RCGlobal::k_SimpleTypeID_OSTexture2D,		//Color Input		
		RCGlobal::k_SimpleTypeID_OSTexture2D,		//Depth Input
		RCGlobal::k_SimpleTypeID_OSTexture2D,		//Occlusion Input
		RCGlobal::k_SimpleTypeID_DVector4f,			//EyePos 
		RCGlobal::k_SimpleTypeID_DVector3f,			//LightDir
		RCGlobal::k_SimpleTypeID_DMatrix4x4f,		//ViewProjInv
		RCGlobal::k_SimpleTypeID_DVector4f,			//colorFog
		RCGlobal::k_SimpleTypeID_DVector4f,			//colorIns
		RCGlobal::k_SimpleTypeID_DMatrix4x4f,		//FogData4x4Matrix
		RCGlobal::k_SimpleTypeID_DVector4f,			//volumeFogAlbedo
		RCGlobal::k_SimpleTypeID_DVector4f,			//volumeFogEmissive
	};
	return InputTypes[i_Index];
}

Int                 MDOperatorVolumetricHeightFog::getOutputCount() const
{
	return 1;
}

DSimpleTypeID       MDOperatorVolumetricHeightFog::getOutputTypeID(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
	DOME_ASSERT(i_ParamCount == nParamCount);
	return RCGlobal::k_SimpleTypeID_OSTexture2D;
}


RCGPUDATAFORMAT     MDOperatorVolumetricHeightFog::getOutputTexFmt(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
	DOME_ASSERT(i_ParamCount == nParamCount);
	DOME_ASSERT(i_pParamList[0] && i_pParamList[0]->isTexture());

	return RGDF_RGBA16F;
}

DResult             MDOperatorVolumetricHeightFog::calcOutputTexSize(Int i_Index, MDEffect* i_pMDEffect, DVector2i& o_Size, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
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
MDOperandPtr        MDOperatorVolumetricHeightFog::execute(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, IRP* o_pInputReleasePoint) const
{
	FRAMETIMER_BEGIN(FTT_RC_CAL_SCENERENDERTO, FTT_RC_CAL_EXECUTE_EFFECTPASS);
	DOME_ASSERT(i_ParamCount == nParamCount);
	DOME_ASSERT(i_pParamList[0] && i_pParamList[0]->isTexture());
	DOME_ASSERT(i_pParamList[1] && i_pParamList[1]->isTexture());
	DOME_ASSERT(i_pParamList[2] && i_pParamList[2]->isTexture());
	DOME_ASSERT(i_pParamList[3] && i_pParamList[3]->isFloat4());
	DOME_ASSERT(i_pParamList[4] && i_pParamList[4]->isFloat3());
	DOME_ASSERT(i_pParamList[5] && i_pParamList[5]->isMatrix4x4());
	DOME_ASSERT(i_pParamList[6] && i_pParamList[6]->isFloat4());
	DOME_ASSERT(i_pParamList[7] && i_pParamList[7]->isFloat4());
	DOME_ASSERT(i_pParamList[8] && i_pParamList[8]->isMatrix4x4());
	DOME_ASSERT(i_pParamList[9] && i_pParamList[9]->isFloat4());
	DOME_ASSERT(i_pParamList[10] && i_pParamList[10]->isFloat4());
	
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


	
	DSimpleTypedValue* l_pColorTexValue		= i_pParamList[0]->getDataPtr();
	DSimpleTypedValue* l_pDepthTexValue		= i_pParamList[1]->getDataPtr();
	DSimpleTypedValue* l_pOcclusionTexValue	= i_pParamList[2]->getDataPtr();
	DSimpleTypedValue* l_pmatParamEyePos	= i_pParamList[3]->getDataPtr();
	DSimpleTypedValue* l_pmatParamLightDir	= i_pParamList[4]->getDataPtr();
	DSimpleTypedValue* l_pmatParamViewProjInv	= i_pParamList[5]->getDataPtr();
	DSimpleTypedValue* l_pmatParamColorFog		= i_pParamList[6]->getDataPtr();
	DSimpleTypedValue* l_pmatParamColorIns		= i_pParamList[7]->getDataPtr();
	DSimpleTypedValue* l_pmatParamFogMatrixData	= i_pParamList[8]->getDataPtr();
	DSimpleTypedValue* l_pmatParamFogAlbedo		= i_pParamList[9]->getDataPtr();
	DSimpleTypedValue* l_pmatParamFogEmissive	= i_pParamList[10]->getDataPtr();

	DResult l_Result;

	DVector2i l_RtSize;
	calcOutputTexSize(0, i_pMDEffect, l_RtSize, i_ParamCount, i_pParamList);

	OSTexture2D l_RtTex;
	l_Result = l_pRenderer->createTexture2D(l_RtTex, l_RtSize.x, l_RtSize.y, 1, RGDF_RGBA16F, RBU_DEFAULT, DM_TRUE, DM_NULL);
	DOME_ASSERT(DM_SUCC(l_Result));

	RCMOD_Texture l_ColorRtTexture;
	*((OSTexture2D*)l_ColorRtTexture.getPtr()) = l_RtTex;

	RCMOD_Texture l_ColorSrcTexture;
	*((OSTexture2D*)l_ColorSrcTexture.getPtr()) = l_pColorTexValue->getValue<OSTexture2D>();

	RCMOD_Texture l_DepthSrcTexture;
	*((OSTexture2D*)l_DepthSrcTexture.getPtr()) = l_pDepthTexValue->getValue<OSTexture2D>();

	RCMOD_Texture l_OcclusionTexture;
	*((OSTexture2D*)l_OcclusionTexture.getPtr()) = l_pOcclusionTexValue->getValue<OSTexture2D>();

	RCMOD_Float4 l_ParamEyePos;
	*(DVector4f*)l_ParamEyePos.getPtr() = (l_pmatParamEyePos->getDVector4f());

	RCMOD_Float3 l_ParamLightDir;
	*(DVector3f*)l_ParamLightDir.getPtr() = (l_pmatParamLightDir->getDVector3f());

	RCMOD_Float4x4 l_ParamViewProjInv;
	*(DMatrix4x4f*)l_ParamViewProjInv.getPtr() = (l_pmatParamViewProjInv->getDMatrix4x4f());

	RCMOD_Float4 l_ParamColorFog;
	*(DVector4f*)l_ParamColorFog.getPtr() = (l_pmatParamColorFog->getDVector4f());

	RCMOD_Float4 l_ParamColorIns;
	*(DVector4f*)l_ParamColorIns.getPtr() = (l_pmatParamColorIns->getDVector4f());


	RCMOD_Float4x4 l_ParamViewFogMatrixData;
	*(DMatrix4x4f*)l_ParamViewFogMatrixData.getPtr() = (l_pmatParamFogMatrixData->getDMatrix4x4f());


	RCMOD_Float4 l_ParamFogAlbedo;
	*(DVector4f*)l_ParamFogAlbedo.getPtr() = (l_pmatParamFogAlbedo->getDVector4f());

	RCMOD_Float4 l_ParamFogEmissive;
	*(DVector4f*)l_ParamFogEmissive.getPtr() = (l_pmatParamFogEmissive->getDVector4f());




	// render
	const static DStringHash k_KEY_ScenePlugin("RCPI_Scene");
	RCPI_Scene* l_pScenePlugin = (RCPI_Scene*)l_pRCEffectMgr->getPlugin(k_KEY_ScenePlugin);

	l_pScenePlugin->RenderVolumetricHeightFog((RCOSRendererData*)l_pRenderer->getOSRendererData(), &l_ColorRtTexture, 
		&l_ColorSrcTexture, &l_DepthSrcTexture, &l_OcclusionTexture,
		l_ParamEyePos, l_ParamLightDir, l_ParamViewProjInv, l_ParamColorFog,
		l_ParamColorIns, l_ParamViewFogMatrixData, l_ParamFogAlbedo, l_ParamFogEmissive);

	//l_pScenePlugin->RenderPostProcessTonemap ((RCOSRendererData*)l_pRenderer->getOSRendererData(), &l_ColorRtTexture, &l_ColorSrcTexture, &l_ExposureTexture, l_Param0, l_Param1, l_Param2, l_Param3, l_Param4, l_Param5);


	MDOperandValue* l_pRtOperand = DOME_New(MDOperandValue)(RCGlobal::k_SimpleTypeID_OSTexture2D);
	l_pRtOperand->getDataPtr()->set(RCGlobal::k_SimpleTypeID_OSTexture2D, &l_RtTex);

	FRAMETIMER_END(FTT_RC_CAL_SCENERENDERTO);

	return l_pRtOperand;
}

DResult             MDOperatorVolumetricHeightFog::destroyResult(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, MDOperandPtr i_pResult) const
{
	RCRenderer* l_pRenderer = i_pMDEffect->getRCEffect()->getEffectManager()->getRenderer();

	OSTexture2D l_hTexture;
	l_hTexture = *i_pResult->getTexturePtr();
	l_pRenderer->destroyTexture2D(l_hTexture);
	DOME_Del(i_pResult);

	return R_SUCCESS;
}


RC_NAMESPACE_END