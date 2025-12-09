#include "pch.h"
/*
    filename:       mdoperatorscenerender.cpp
    author:         Ming Dong
    date:           2016-JUN-28
    description:    
*/

#include "mdoperatorblendskybox.h"
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
MDOperatorBlendSkybox::MDOperatorBlendSkybox()
    : m_OperatorName("MDBlendSKybox")
{

}

MDOperatorBlendSkybox::~MDOperatorBlendSkybox()
{

}

/****************************
    FROM MDOperator class
****************************/
const DString& MDOperatorBlendSkybox::getOperatorName() const
{
    return m_OperatorName;
}

Bool                MDOperatorBlendSkybox::isGpuOperator() const
{
    return DM_FALSE;
}

Int                 MDOperatorBlendSkybox::getInputCount() const
{
    return nParamCount;
}

DSimpleTypeID       MDOperatorBlendSkybox::getInputTypeID(Int i_Index) const
{
	DOME_ASSERT(i_Index < 0 || i_Index >= nParamCount);
	DSimpleTypeID InputTypes[nParamCount] = {
		RCGlobal::k_SimpleTypeID_OSTexture2D,				
		RCGlobal::k_SimpleTypeID_OSTexture2D,		
		RCGlobal::k_SimpleTypeID_OSTexture2D,		
		RCGlobal::k_SimpleTypeID_DVector3f,
		RCGlobal::k_SimpleTypeID_F32,
		RCGlobal::k_SimpleTypeID_F32,
		RCGlobal::k_SimpleTypeID_DVector4f,
		RCGlobal::k_SimpleTypeID_DVector2f,
		RCGlobal::k_SimpleTypeID_F32	
	};
	return InputTypes[i_Index];
}

Int                 MDOperatorBlendSkybox::getOutputCount() const
{
    return 1;
}

DSimpleTypeID       MDOperatorBlendSkybox::getOutputTypeID(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_ParamCount == nParamCount);
    return RCGlobal::k_SimpleTypeID_OSTexture2D;
}

RCGPUDATAFORMAT     MDOperatorBlendSkybox::getOutputTexFmt(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
	DOME_ASSERT(i_ParamCount == nParamCount);
	DOME_ASSERT(i_pParamList[0] && i_pParamList[0]->isTexture());

	return RGDF_RGBA16F;
}

DResult             MDOperatorBlendSkybox::calcOutputTexSize(Int i_Index, MDEffect* i_pMDEffect, DVector2i& o_Size, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
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
MDOperandPtr        MDOperatorBlendSkybox::execute(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, IRP* o_pInputReleasePoint) const
{
	FRAMETIMER_BEGIN(FTT_RC_CAL_SCENERENDERTO, FTT_RC_CAL_EXECUTE_EFFECTPASS);
	DOME_ASSERT(i_ParamCount == nParamCount);
	DOME_ASSERT(i_pParamList[0] && i_pParamList[0]->isTexture());
	DOME_ASSERT(i_pParamList[1] && i_pParamList[1]->isTexture());
	DOME_ASSERT(i_pParamList[2] && i_pParamList[2]->isTexture());
	DOME_ASSERT(i_pParamList[3] && i_pParamList[3]->isFloat3());
	DOME_ASSERT(i_pParamList[4] && i_pParamList[4]->isFloat());
	DOME_ASSERT(i_pParamList[5] && i_pParamList[5]->isFloat());
	DOME_ASSERT(i_pParamList[6] && i_pParamList[6]->isFloat4());
	DOME_ASSERT(i_pParamList[7] && i_pParamList[7]->isFloat2());
	DOME_ASSERT(i_pParamList[8] && i_pParamList[8]->isFloat());
	RCEffectManager* l_pRCEffectMgr = i_pMDEffect->getRCEffect()->getEffectManager();
	RCRenderer* l_pRenderer = l_pRCEffectMgr->getRenderer();

	o_pInputReleasePoint[0] = IRP_INFINISHCALLBACK; //output
	o_pInputReleasePoint[1] = IRP_AFTEREXECUTE;	
	o_pInputReleasePoint[2] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[3] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[4] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[5] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[6] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[7] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[8] = IRP_AFTEREXECUTE;
//	DResult l_Result;

	DSimpleTypedValue* l_pColorOutputValue = i_pParamList[0]->getDataPtr();	
	RCMOD_Texture l_ColorOutputTexture;
	*((OSTexture2D*)l_ColorOutputTexture.getPtr()) = l_pColorOutputValue->getValue<OSTexture2D>();

	DSimpleTypedValue* l_pDepthInputValue = i_pParamList[1]->getDataPtr();
	RCMOD_Texture l_DepthInputTexture;
	*((OSTexture2D*)l_DepthInputTexture.getPtr()) = l_pDepthInputValue->getValue<OSTexture2D>();

	DSimpleTypedValue* l_pSrcTextureValue = i_pParamList[2]->getDataPtr();
	RCMOD_Texture l_SrcTexture;
	*((OSTexture2D*)l_SrcTexture.getPtr()) = l_pSrcTextureValue->getValue<OSTexture2D>();

	DSimpleTypedValue* l_pf3PosDir = i_pParamList[3]->getDataPtr();
	RCMOD_Float3 l_pf3PosDirParam;
	*(DVector3f*)l_pf3PosDirParam.getPtr() = (l_pf3PosDir->getDVector3f());

	DSimpleTypedValue* l_pScaleAngle = i_pParamList[4]->getDataPtr();
	float l_pScaleAngleParam = l_pScaleAngle->getValue<float>();
	

	DSimpleTypedValue* l_pRotAngle = i_pParamList[5]->getDataPtr();
	float l_pRotAngleParam = l_pRotAngle->getValue<float>();
	
	DSimpleTypedValue* l_pCastColor = i_pParamList[6]->getDataPtr();
	RCMOD_Float4 l_pCastColorParam;
	*(DVector4f*)l_pCastColorParam.getPtr() = (l_pCastColor->getDVector4f());

	DSimpleTypedValue* l_pTextureDimension = i_pParamList[7]->getDataPtr();
	RCMOD_Float2 l_pTextureDimensionParam;
	*(DVector2f*)l_pTextureDimensionParam.getPtr() = (l_pTextureDimension->getDVector2f());
	
	DSimpleTypedValue* l_pFrameRate = i_pParamList[8]->getDataPtr();
	float l_pFrameRateParam = (l_pFrameRate->getValue<float>());;
	


	// render
	const static DStringHash k_KEY_ScenePlugin("RCPI_Scene");
	RCPI_Scene* l_pScenePlugin = (RCPI_Scene*)l_pRCEffectMgr->getPlugin(k_KEY_ScenePlugin);

	l_pScenePlugin->RenderBlendSkybox((RCOSRendererData*)l_pRenderer->getOSRendererData(), &l_ColorOutputTexture, 
		&l_DepthInputTexture, &l_SrcTexture, l_pf3PosDirParam, l_pScaleAngleParam, l_pRotAngleParam, l_pCastColorParam, l_pTextureDimensionParam, l_pFrameRateParam);

	MDOperandValue* l_pRtOperand = DOME_New(MDOperandValue)(RCGlobal::k_SimpleTypeID_OSTexture2D);
	l_pRtOperand->getDataPtr()->set(RCGlobal::k_SimpleTypeID_OSTexture2D, l_ColorOutputTexture.getPtr());

	FRAMETIMER_END(FTT_RC_CAL_SCENERENDERTO);

	return l_pRtOperand;
}

DResult             MDOperatorBlendSkybox::destroyResult(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, MDOperandPtr i_pResult) const
{
	return R_SUCCESS;
}


RC_NAMESPACE_END