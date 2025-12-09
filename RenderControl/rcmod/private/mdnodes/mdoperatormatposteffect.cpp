#include "pch.h"
#include "mdoperatormatposteffect.h"
#include <rc/public/iexecuter.h>

#ifdef RC_PERF
#include "KG3D_FrameTimer.h"
#else
#define FRAMETIMER_BEGIN(a,b)
#define FRAMETIMER_END(a)
#endif


RC_NAMESPACE_BEGIN

static const unsigned int nParamCount = 5;
MDOperatorMatPostEffect::MDOperatorMatPostEffect()
    : m_OperatorName("MDMatPostEffect")
{

}

MDOperatorMatPostEffect::~MDOperatorMatPostEffect()
{

}

const DString& MDOperatorMatPostEffect::getOperatorName() const
{
    return m_OperatorName;
}

Bool                MDOperatorMatPostEffect::isGpuOperator() const
{
    return DM_FALSE;
}

Int                 MDOperatorMatPostEffect::getInputCount() const
{
    return nParamCount;
}

DSimpleTypeID       MDOperatorMatPostEffect::getInputTypeID(Int i_Index) const
{
	DOME_ASSERT(i_Index < 0 || i_Index >= nParamCount);
	DSimpleTypeID InputTypes[nParamCount] = {
		RCGlobal::k_SimpleTypeID_OSTexture2D,		
		RCGlobal::k_SimpleTypeID_OSTexture2D,
		RCGlobal::k_SimpleTypeID_OSTexture2D,
		RCGlobal::k_SimpleTypeID_F32,
		RCGlobal::k_SimpleTypeID_DVector4f,
	};
	return InputTypes[i_Index];
}

Int                 MDOperatorMatPostEffect::getOutputCount() const
{
    return 1;
}

DSimpleTypeID       MDOperatorMatPostEffect::getOutputTypeID(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_ParamCount == nParamCount);
    return RCGlobal::k_SimpleTypeID_OSTexture2D;
}

RCGPUDATAFORMAT     MDOperatorMatPostEffect::getOutputTexFmt(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
	DOME_ASSERT(i_ParamCount == nParamCount);

	return RGDF_RGBA16F;
}

DResult             MDOperatorMatPostEffect::calcOutputTexSize(Int i_Index, MDEffect* i_pMDEffect, DVector2i& o_Size, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
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
MDOperandPtr        MDOperatorMatPostEffect::execute(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, IRP* o_pInputReleasePoint) const
{
    PERF_COUNTER_EX(0);
	FRAMETIMER_BEGIN(FTT_RC_CAL_FOG_RAYMARCH, FTT_RC_CAL_EXECUTE_EFFECTPASS);
	DOME_ASSERT(i_ParamCount == nParamCount);
	DOME_ASSERT(i_pParamList[0] && i_pParamList[0]->isTexture());
	DOME_ASSERT(i_pParamList[1] && i_pParamList[1]->isTexture());
	DOME_ASSERT(i_pParamList[2] && i_pParamList[2]->isTexture());
	
	DResult l_Result;
    
    RCEffectManager* l_pRCEffectMgr = i_pMDEffect->getRCEffect()->getEffectManager();
    RCRenderer* l_pRenderer = l_pRCEffectMgr->getRenderer();

	o_pInputReleasePoint[0] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[1] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[2] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[3] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[4] = IRP_AFTEREXECUTE;

	DSimpleTypedValue* l_pGBuffer0Value	= i_pParamList[0]->getDataPtr();
	DSimpleTypedValue* l_pGBuffer1Value	= i_pParamList[1]->getDataPtr();
	DSimpleTypedValue* l_pGBuffer2Value	= i_pParamList[2]->getDataPtr();
	DSimpleTypedValue* l_pParamF1Value  = i_pParamList[3]->getDataPtr();
	DSimpleTypedValue* l_pParamF4Value	= i_pParamList[4]->getDataPtr();
	
	DVector2i l_RtSize;
	l_Result = calcOutputTexSize(0, i_pMDEffect, l_RtSize, i_ParamCount, i_pParamList);
	DOME_ASSERT(DM_SUCC(l_Result));

	RCGPUDATAFORMAT l_Format = getOutputTexFmt(0, i_pMDEffect, i_ParamCount, i_pParamList);
	DOME_ASSERT(l_Format == RGDF_RGBA8 || l_Format == RGDF_RGBA16F);

	OSTexture2D l_RtTex;
	l_Result = l_pRenderer->createTexture2D(l_RtTex, l_RtSize.x, l_RtSize.y, 1, l_Format, RBU_DEFAULT, DM_TRUE, DM_NULL);
	DOME_ASSERT(DM_SUCC(l_Result));

	RCMOD_Texture l_GBuffer0Texture;
	*((OSTexture2D*)l_GBuffer0Texture.getPtr()) = l_pGBuffer0Value->getValue<OSTexture2D>();

	RCMOD_Texture l_GBuffer1Texture;
	*((OSTexture2D*)l_GBuffer1Texture.getPtr()) = l_pGBuffer1Value->getValue<OSTexture2D>();

	RCMOD_Texture l_GBuffer2Texture;
	*((OSTexture2D*)l_GBuffer2Texture.getPtr()) = l_pGBuffer2Value->getValue<OSTexture2D>();

    RCMOD_Texture l_ColorOutputTexture;
	*((OSTexture2D*)l_ColorOutputTexture.getPtr()) = l_RtTex;


	float l_ParamF1;
	l_ParamF1 = l_pParamF1Value->getF32();

	RCMOD_Float4 l_ParamF4;
	*(DVector4f*)l_ParamF4.getPtr() = (l_pParamF4Value->getDVector4f());

    // render
    const static DStringHash k_KEY_ScenePlugin("RCPI_Scene");
    RCPI_Scene* l_pScenePlugin = (RCPI_Scene*)l_pRCEffectMgr->getPlugin(k_KEY_ScenePlugin);
	
	l_pScenePlugin->RenderMatPostEffect(
		(RCOSRendererData*)l_pRenderer->getOSRendererData(),
		&l_GBuffer0Texture,
		&l_GBuffer1Texture,
		&l_GBuffer2Texture,
		l_ParamF1,
		l_ParamF4,
		&l_ColorOutputTexture
	); 

	MDOperandValue* l_pRtOperand = DOME_New(MDOperandValue)(RCGlobal::k_SimpleTypeID_OSTexture2D);

	l_Result = l_pRtOperand->getDataPtr()->set(RCGlobal::k_SimpleTypeID_OSTexture2D, &l_RtTex);
	DOME_ASSERT(DM_SUCC(l_Result));

	return l_pRtOperand;
}

DResult             MDOperatorMatPostEffect::destroyResult(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, MDOperandPtr i_pResult) const
{
	RCRenderer* l_pRenderer = i_pMDEffect->getRCEffect()->getEffectManager()->getRenderer();

	OSTexture2D l_hTexture;
	l_hTexture = *i_pResult->getTexturePtr();
	l_pRenderer->destroyTexture2D(l_hTexture);
	DOME_Del(i_pResult);

	return R_SUCCESS;
}


RC_NAMESPACE_END