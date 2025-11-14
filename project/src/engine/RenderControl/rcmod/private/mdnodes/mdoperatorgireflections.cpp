#include "pch.h"

#include "mdoperatorgireflections.h"
#include <rc/public/iexecuter.h>

#ifdef RC_PERF
#include "KG3D_FrameTimer.h"
#else
#define FRAMETIMER_BEGIN(a,b)
#define FRAMETIMER_END(a)
#endif


RC_NAMESPACE_BEGIN

// MDSceneRender($F4)
static const int s_InputCount = 6;
MDOperatorGIReflections::MDOperatorGIReflections()
	: m_OperatorName("MDGIReflections")
{

}

MDOperatorGIReflections::~MDOperatorGIReflections()
{

}

/****************************
FROM MDOperator class
****************************/
const DString&      MDOperatorGIReflections::getOperatorName() const
{
	return m_OperatorName;
}

Bool                MDOperatorGIReflections::isGpuOperator() const
{
	return DM_FALSE;
}

Int                 MDOperatorGIReflections::getInputCount() const
{
	return s_InputCount;
}

DSimpleTypeID       MDOperatorGIReflections::getInputTypeID(Int i_Index) const
{
	DOME_ASSERT(i_Index < 0 || i_Index >= s_InputCount);
	DSimpleTypeID InputTypes[s_InputCount] = {
		RCGlobal::k_SimpleTypeID_OSTexture2D,
		RCGlobal::k_SimpleTypeID_OSTexture2D,
		RCGlobal::k_SimpleTypeID_OSTexture2D,
		RCGlobal::k_SimpleTypeID_OSTexture2D,
		RCGlobal::k_SimpleTypeID_OSTexture2D,
		RCGlobal::k_SimpleTypeID_OSTexture2D
	};
	return InputTypes[i_Index];
}

Int                 MDOperatorGIReflections::getOutputCount() const
{
	return 1;
}

DSimpleTypeID       MDOperatorGIReflections::getOutputTypeID(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
	return RCGlobal::k_SimpleTypeID_OSTexture2D;
}

RCGPUDATAFORMAT     MDOperatorGIReflections::getOutputTexFmt(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
	return RGDF_RGBA16F;
}

DResult             MDOperatorGIReflections::calcOutputTexSize(Int i_Index, MDEffect* i_pMDEffect, DVector2i& o_Size, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
	DOME_ASSERT(i_Index == 0);
	DOME_ASSERT(i_ParamCount == s_InputCount);

	DResult l_Result;

	l_Result = i_pParamList[0]->getTextureSize(o_Size);
	DOME_ASSERT(DM_SUCC(l_Result));
	return R_SUCCESS;
}

/****************************
FROM MDOperatorCpu class
****************************/
MDOperandPtr        MDOperatorGIReflections::execute(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, IRP* o_pInputReleasePoint) const
{
	DOME_ASSERT(i_ParamCount == s_InputCount);
	DOME_ASSERT(i_pParamList[0] && i_pParamList[0]->isTexture());
	DOME_ASSERT(i_pParamList[1] && i_pParamList[1]->isTexture());
	DOME_ASSERT(i_pParamList[2] && i_pParamList[2]->isTexture());
	DOME_ASSERT(i_pParamList[3] && i_pParamList[3]->isTexture());
	DOME_ASSERT(i_pParamList[4] && i_pParamList[4]->isTexture());
	DOME_ASSERT(i_pParamList[5] && i_pParamList[4]->isTexture());

	RCEffectManager* l_pRCEffectMgr = i_pMDEffect->getRCEffect()->getEffectManager();
	RCRenderer* l_pRenderer = l_pRCEffectMgr->getRenderer();

	DResult l_Result;
	o_pInputReleasePoint[0] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[1] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[2] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[3] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[4] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[5] = IRP_AFTEREXECUTE;

	DVector2i l_RtSize;
	l_Result = calcOutputTexSize(0, i_pMDEffect, l_RtSize, i_ParamCount, i_pParamList);
	DOME_ASSERT(DM_SUCC(l_Result));

	RCGPUDATAFORMAT l_Format = getOutputTexFmt(0, i_pMDEffect, i_ParamCount, i_pParamList);
	DOME_ASSERT(l_Format == RGDF_RGBA8 || l_Format == RGDF_RGBA16F);

	OSTexture2D l_RtTex;
	l_Result = l_pRenderer->createTexture2D(l_RtTex, l_RtSize.x, l_RtSize.y, 1, l_Format, RBU_DEFAULT, DM_TRUE, DM_NULL, 1);
	DOME_ASSERT(DM_SUCC(l_Result));

	RCMOD_Texture l_ColorTexture;
	*((OSTexture2D*)l_ColorTexture.getPtr()) = l_RtTex;
	
#define GetTexture(name, index) \
	RCMOD_Texture l_##name; \
	DSimpleTypedValue* l_p##name = i_pParamList[index]->getDataPtr();\
	*((OSTexture2D*)l_##name.getPtr()) = l_p##name->getValue<OSTexture2D>();
	
	GetTexture(ResolveDepth, 0);
	GetTexture(GBuffer0, 1);
	GetTexture(GBuffer1, 2);
	GetTexture(GBuffer2, 3);
	GetTexture(GBuffer3, 4);
	GetTexture(GBuffer4, 5);

	RCMOD_Texture* l_OutputTextures[] = {
		&l_ColorTexture
	};

	RCMOD_Texture* l_InputTextures[] = {
		&l_GBuffer0,
		&l_GBuffer1,
		&l_GBuffer2,
		&l_GBuffer3,
		&l_GBuffer4,
		&l_ResolveDepth,
	};

	// render
	const static DStringHash k_KEY_ScenePlugin("RCPI_Scene");
	RCPI_Scene* l_pScenePlugin = (RCPI_Scene*)l_pRCEffectMgr->getPlugin(k_KEY_ScenePlugin);

	l_pScenePlugin->RenderGIRefelctions(
		(RCOSRendererData*)l_pRenderer->getOSRendererData(),
		l_InputTextures,
		sizeof(l_InputTextures) / sizeof(l_InputTextures[0]),
		l_OutputTextures,
		sizeof(l_OutputTextures) / sizeof(l_OutputTextures[0]));

	MDOperandValue* l_pRtOperand = DOME_New(MDOperandValue)(RCGlobal::k_SimpleTypeID_OSTexture2D);
	l_Result = l_pRtOperand->getDataPtr()->set(RCGlobal::k_SimpleTypeID_OSTexture2D, &l_RtTex);
	DOME_ASSERT(DM_SUCC(l_Result));

	FRAMETIMER_END(FTT_RC_CAL_SSR);
	return l_pRtOperand;
}

DResult             MDOperatorGIReflections::destroyResult(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, MDOperandPtr i_pResult) const
{
	RCRenderer* l_pRenderer = i_pMDEffect->getRCEffect()->getEffectManager()->getRenderer();

	OSTexture2D l_hTexture;
	l_hTexture = *i_pResult->getTexturePtr();
	l_pRenderer->destroyTexture2D(l_hTexture);
	DOME_Del(i_pResult);

	return R_SUCCESS;
}


RC_NAMESPACE_END