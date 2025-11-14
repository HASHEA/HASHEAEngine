#include "pch.h"
#include "mdoperatorrenderdynamicweathermask.h"
#include <rc/public/iexecuter.h>

#ifdef RC_PERF
#include "KG3D_FrameTimer.h"
#else
#define FRAMETIMER_BEGIN(a,b)
#define FRAMETIMER_END(a)
#endif

RC_NAMESPACE_BEGIN

static unsigned int nParamCount = 14;

MDOperatorRenderDynamicWeatherMask::MDOperatorRenderDynamicWeatherMask()
	: m_OperatorName("MDRenderDynamicWeatherMask")
{

}

MDOperatorRenderDynamicWeatherMask::~MDOperatorRenderDynamicWeatherMask()
{

}

/****************************
	FROM MDOperator class
****************************/
const DString& MDOperatorRenderDynamicWeatherMask::getOperatorName() const
{
	return m_OperatorName;
}

Bool                MDOperatorRenderDynamicWeatherMask::isGpuOperator() const
{
	return DM_FALSE;
}

Int                 MDOperatorRenderDynamicWeatherMask::getInputCount() const
{
	return nParamCount;
}

DSimpleTypeID       MDOperatorRenderDynamicWeatherMask::getInputTypeID(Int i_Index) const
{
	switch (i_Index)
	{
	case 0:
	case 1:
	case 4:
	case 5:
	case 7:
	case 11:
		return RCGlobal::k_SimpleTypeID_OSTexture2D;
	case 2:
	case 3:
	case 6:
	case 8:
	case 9:
		return RCGlobal::k_SimpleTypeID_DMatrix4x4f;
	case 10:
		return RCGlobal::k_SimpleTypeID_DMatrix3x3f;
	case 12:
		return RCGlobal::k_SimpleTypeID_DVector4f;
	case 13:
		return RCGlobal::k_SimpleTypeID_F32;
	}
	return RCGlobal::k_SimpleTypeID_Unknown;
}

Int                 MDOperatorRenderDynamicWeatherMask::getOutputCount() const
{
	return 1;
}

DSimpleTypeID       MDOperatorRenderDynamicWeatherMask::getOutputTypeID(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
	DOME_ASSERT(i_Index == 0);
	return RCGlobal::k_SimpleTypeID_OSTexture2D;
}

RCGPUDATAFORMAT     MDOperatorRenderDynamicWeatherMask::getOutputTexFmt(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
	DOME_ASSERT(i_Index == 0);
	DOME_ASSERT(i_ParamCount == nParamCount);

	return RGDF_RGBA8;
}

DResult             MDOperatorRenderDynamicWeatherMask::calcOutputTexSize(Int i_Index, MDEffect* i_pMDEffect, DVector2i& o_Size, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
	DOME_ASSERT(i_Index == 0);
	DOME_ASSERT(i_ParamCount == nParamCount);
	DOME_ASSERT(i_pParamList[0]->isTexture());      // G Buffer 0
	DOME_ASSERT(i_pParamList[1]->isTexture());      // depth texture
	DOME_ASSERT(i_pParamList[2]->isMatrix4x4());    // CamViewInv
	DOME_ASSERT(i_pParamList[3]->isMatrix4x4());    // TopCamMVP
	DOME_ASSERT(i_pParamList[4]->isTexture());      // TopCamDepthTex
	DOME_ASSERT(i_pParamList[5]->isTexture());      // WaterMask
	DOME_ASSERT(i_pParamList[6]->isMatrix4x4());    // Param
	DOME_ASSERT(i_pParamList[7]->isTexture());		// Noise
	DOME_ASSERT(i_pParamList[8]->isMatrix4x4());    // TopCamProj
	DOME_ASSERT(i_pParamList[9]->isMatrix4x4());    // CamProjInv
	DOME_ASSERT(i_pParamList[10]->isMatrix3x3());   // WaterAvgHeights
	DOME_ASSERT(i_pParamList[11]->isTexture());      // SeparateMask
	DOME_ASSERT(i_pParamList[12]->isFloat4());      // eye position
	DOME_ASSERT(i_pParamList[13]->isFloat());      // Ocean Height

	return i_pParamList[1]->getTextureSize(o_Size);
}

/****************************
	FROM MDOperatorCpu class
****************************/
MDOperandPtr        MDOperatorRenderDynamicWeatherMask::execute(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, IRP* o_pInputReleasePoint) const
{
	FRAMETIMER_BEGIN(FTT_RC_CAL_ACCUMULATER, FTT_RC_CAL_EXECUTE_EFFECTPASS);

	DResult l_Result;

	DOME_ASSERT(i_ParamCount == nParamCount);
	DOME_ASSERT(i_pParamList[0]->isTexture());      // G Buffer 0
	DOME_ASSERT(i_pParamList[1]->isTexture());      // depth texture
	DOME_ASSERT(i_pParamList[2]->isMatrix4x4());    // CamViewInv
	DOME_ASSERT(i_pParamList[3]->isMatrix4x4());    // TopCamView
	DOME_ASSERT(i_pParamList[4]->isTexture());      // TopCamDepthTex
	DOME_ASSERT(i_pParamList[5]->isTexture());      // WaterMask
	DOME_ASSERT(i_pParamList[6]->isMatrix4x4());    // Param
	DOME_ASSERT(i_pParamList[7]->isTexture());		// Noise
	DOME_ASSERT(i_pParamList[8]->isMatrix4x4());    // TopCamProj
	DOME_ASSERT(i_pParamList[9]->isMatrix4x4());    // CamProjInv
	DOME_ASSERT(i_pParamList[10]->isMatrix3x3());   // WaterAvgHeights
	DOME_ASSERT(i_pParamList[11]->isTexture());      // SeparateMask
	DOME_ASSERT(i_pParamList[12]->isFloat4());      // eye position
	DOME_ASSERT(i_pParamList[13]->isFloat());      // Ocean Height

	RCEffectManager* l_pEffectMgr = i_pMDEffect->getRCEffect()->getEffectManager();
	RCRenderer* l_pRenderer = l_pEffectMgr->getRenderer();

	OSTexture2D l_MaskTex;
	RCGPUDATAFORMAT l_Format;
	DVector2i l_TexSize;

	l_Result = calcOutputTexSize(0, i_pMDEffect, l_TexSize, i_ParamCount, i_pParamList);
	DOME_ASSERT(DM_SUCC(l_Result));

	l_Format = getOutputTexFmt(0, i_pMDEffect, i_ParamCount, i_pParamList);

	l_Result = l_pRenderer->createTexture2D(l_MaskTex, l_TexSize.x, l_TexSize.y, 1, l_Format, RBU_DEFAULT, DM_TRUE, DM_NULL);
	DOME_ASSERT(DM_SUCC(l_Result));

	RCMOD_Texture l_OutputMaskTexure;
	*((OSTexture2D*)l_OutputMaskTexure.getPtr()) = l_MaskTex;

	o_pInputReleasePoint[0] = IRP_INFINISHCALLBACK;
	o_pInputReleasePoint[1] = IRP_INFINISHCALLBACK;
	o_pInputReleasePoint[2] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[3] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[4] = IRP_INFINISHCALLBACK;
	o_pInputReleasePoint[5] = IRP_INFINISHCALLBACK;
	o_pInputReleasePoint[6] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[7] = IRP_INFINISHCALLBACK;
	o_pInputReleasePoint[8] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[9] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[10] = IRP_INFINISHCALLBACK;
	o_pInputReleasePoint[11] = IRP_INFINISHCALLBACK;
	o_pInputReleasePoint[12] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[13] = IRP_AFTEREXECUTE;

	OSTexture2D l_Gbuffer0 = *i_pParamList[0]->getTexturePtr();
	OSTexture2D l_Depth = *i_pParamList[1]->getTexturePtr();
	DMatrix4x4f l_MVPI = *i_pParamList[2]->getMatrix4x4Ptr();
	DMatrix4x4f l_TopCamView = *i_pParamList[3]->getMatrix4x4Ptr();
	OSTexture2D l_TopCamDepthTex = *i_pParamList[4]->getTexturePtr();
	OSTexture2D l_WaterDepth = *i_pParamList[5]->getTexturePtr();
	DMatrix4x4f l_Param = *i_pParamList[6]->getMatrix4x4Ptr();
	OSTexture2D l_Noise = *i_pParamList[7]->getTexturePtr();
	DMatrix4x4f l_TopCamProj = *i_pParamList[8]->getMatrix4x4Ptr();
	DMatrix4x4f l_CamProjInv = *i_pParamList[9]->getMatrix4x4Ptr();
	DMatrix3x3f l_WaterAvgHeights = *i_pParamList[10]->getMatrix3x3Ptr();
	OSTexture2D l_SeparateMask = *i_pParamList[11]->getTexturePtr();
	DVector4f l_EyePos = *i_pParamList[12]->getFloat4Ptr();
	F32 l_OceanHeight = *i_pParamList[13]->getFloatPtr();

	F32 fWeatherType = l_Param.M(0, 3);

	if (fWeatherType == 1)
	{
		const static DStringHash k_KEY_ScenePlugin("RCPI_Scene");
		RCPI_Scene* l_pScenePlugin = (RCPI_Scene*)l_pEffectMgr->getPlugin(k_KEY_ScenePlugin);

		RCMOD_Texture MaskTexture;
		*((OSTexture2D*)MaskTexture.getPtr()) = l_MaskTex;

		RCMOD_Texture GBuffer0Texture;
		*((OSTexture2D*)GBuffer0Texture.getPtr()) = l_Gbuffer0;

		RCMOD_Texture DepthTexture;
		*((OSTexture2D*)DepthTexture.getPtr()) = l_Depth;

		RCMOD_Texture WaterDepthTexture;
		*((OSTexture2D*)WaterDepthTexture.getPtr()) = l_WaterDepth;

		RCMOD_Texture NoiseTexture;
		*((OSTexture2D*)NoiseTexture.getPtr()) = l_Noise;

		RCMOD_Float4x4 Param;
		*((DMatrix4x4f*)Param.getPtr()) = l_Param;

		F32 fUseSkyVisibility = l_Param.M(3, 3);

		if (fUseSkyVisibility > 0)
		{
			RCEffectManager* l_pRCEffectMgr = i_pMDEffect->getRCEffect()->getEffectManager();
			RCRenderer* l_pRenderer = l_pRCEffectMgr->getRenderer();

			l_pScenePlugin->RenderDynamicWeatherSnowMask((RCOSRendererData*)l_pRenderer->getOSRendererData(),
				&MaskTexture, &GBuffer0Texture, &DepthTexture, &WaterDepthTexture, &NoiseTexture, &Param);
		}
		else
		{
			IExecuter* l_pExecuter = (IExecuter*)l_pEffectMgr->getMDExecuter(DHashString("MDRenderDynamicWeatherMaskGPU($TEX[0],$TEX[1],$M44[0],$M44[1],$TEX[2],$TEX[3],$M44[2],$TEX[4],$M44[3],$M44[4],$M33[0],$TEX[5],$F4[0],$F1[0])"));
			DOME_ASSERT(l_pExecuter);

			l_pExecuter->setRenderTarget(l_MaskTex);
			l_pExecuter->setRenderTargetViewport(Int(0),
				Int(0),
				Int(l_TexSize.x),
				Int(l_TexSize.y));

			l_pExecuter->setUVCoef(DVector4f(1.0f, 1.0f, 0.0f, 0.0f));
			l_pExecuter->setTextureParam(0, l_Gbuffer0);
			l_pExecuter->setTextureParam(1, l_Depth);
			l_pExecuter->setMatrix4x4Param(0, l_MVPI);
			l_pExecuter->setMatrix4x4Param(1, l_TopCamView);


			l_pExecuter->setTextureParam(2, l_TopCamDepthTex);


			l_pExecuter->setTextureParam(3, l_WaterDepth);
			l_pExecuter->setMatrix4x4Param(2, l_Param);
			l_pExecuter->setTextureParam(4, l_Noise);

			l_pExecuter->setMatrix4x4Param(3, l_TopCamProj);

			l_pExecuter->setMatrix4x4Param(4, l_CamProjInv);


			l_pExecuter->setMatrix3x3Param(0, l_WaterAvgHeights);
			l_pExecuter->setTextureParam(5, l_SeparateMask);
			l_pExecuter->setFloat4Param(0, l_EyePos);
			l_pExecuter->setFloat1Param(0, l_OceanHeight);

			l_Result = l_pExecuter->execute();
		}
	}
	else if (fWeatherType == 2)
	{
		IExecuter* l_pExecuter = (IExecuter*)l_pEffectMgr->getMDExecuter(DHashString("MDRenderDynamicWeatherRainMaskGPU($TEX[0],$TEX[1],$M44[0],$M44[1],$TEX[2],$TEX[3],$M44[2],$TEX[4],$M44[3],$M44[4],$F4[0])"));
		DOME_ASSERT(l_pExecuter);

		l_pExecuter->setRenderTarget(l_MaskTex);
		l_pExecuter->setRenderTargetViewport(Int(0),
			Int(0),
			Int(l_TexSize.x),
			Int(l_TexSize.y));

		l_pExecuter->setUVCoef(DVector4f(1.0f, 1.0f, 0.0f, 0.0f));
		l_pExecuter->setTextureParam(0, l_Gbuffer0);
		l_pExecuter->setTextureParam(1, l_Depth);
		l_pExecuter->setMatrix4x4Param(0, l_MVPI);
		l_pExecuter->setMatrix4x4Param(1, l_TopCamView);
		l_pExecuter->setTextureParam(2, l_TopCamDepthTex);
		l_pExecuter->setTextureParam(3, l_WaterDepth);
		l_pExecuter->setMatrix4x4Param(2, l_Param);
		l_pExecuter->setTextureParam(4, l_Noise);
		l_pExecuter->setMatrix4x4Param(3, l_TopCamProj);
		l_pExecuter->setMatrix4x4Param(4, l_CamProjInv);
		l_pExecuter->setFloat4Param(0, l_EyePos);
		l_Result = l_pExecuter->execute();
	}
	else
	{
		DOME_ASSERT(false);
	}



	DOME_ASSERT(DM_SUCC(l_Result));
	FRAMETIMER_END(FTT_RC_CAL_ACCUMULATER);

	MDOperandValue* l_pMaskOperand = DOME_New(MDOperandValue)(RCGlobal::k_SimpleTypeID_OSTexture2D);
	l_Result = l_pMaskOperand->getDataPtr()->set(RCGlobal::k_SimpleTypeID_OSTexture2D, &l_MaskTex);
	DOME_ASSERT(DM_SUCC(l_Result));

	MDOperandArray* l_pOperand = DOME_New(MDOperandArray);
	l_pOperand->addOperand(l_pMaskOperand);

	return l_pOperand;
}

DResult             MDOperatorRenderDynamicWeatherMask::destroyResult(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, MDOperandPtr i_pResult) const
{
	RCRenderer* l_pRenderer = i_pMDEffect->getRCEffect()->getEffectManager()->getRenderer();

	OSTexture2D l_hTexture;
	MDOperandValue* l_pSubOperand = (MDOperandValue*)i_pResult->getSubOperand(0);
	l_hTexture = *l_pSubOperand->getTexturePtr();
	l_pRenderer->destroyTexture2D(l_hTexture);
	DOME_Del(l_pSubOperand);

	DOME_Del(i_pResult);

	return R_SUCCESS;
}


RC_NAMESPACE_END