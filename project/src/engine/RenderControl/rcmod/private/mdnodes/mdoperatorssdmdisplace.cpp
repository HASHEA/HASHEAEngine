#include "pch.h"

#include "mdoperatorssdmdisplace.h"
#include <rc/public/iexecuter.h>

#ifdef RC_PERF
#include "KG3D_FrameTimer.h"
#else
#define FRAMETIMER_BEGIN(a,b)
#define FRAMETIMER_END(a)
#endif


RC_NAMESPACE_BEGIN

// MDSceneRender($F4)
static const int s_InputCount = 2;
MDOperatorSSDMDisplace::MDOperatorSSDMDisplace()
	: m_OperatorName("MDSSDMDisplace")
{

}

MDOperatorSSDMDisplace::~MDOperatorSSDMDisplace()
{

}

/****************************
	FROM MDOperator class
****************************/
const DString& MDOperatorSSDMDisplace::getOperatorName() const
{
	return m_OperatorName;
}

Bool                MDOperatorSSDMDisplace::isGpuOperator() const
{
	return DM_FALSE;
}

Int                 MDOperatorSSDMDisplace::getInputCount() const
{
	return s_InputCount;
}

DSimpleTypeID       MDOperatorSSDMDisplace::getInputTypeID(Int i_Index) const
{
	DOME_ASSERT(i_Index < 0 || i_Index >= s_InputCount);
	DSimpleTypeID InputTypes[s_InputCount] = {
		RCGlobal::k_SimpleTypeID_OSTexture2D,
		RCGlobal::k_SimpleTypeID_OSTexture2D,
	};
	return InputTypes[i_Index];
}

Int                 MDOperatorSSDMDisplace::getOutputCount() const
{
	return 1;
}

DSimpleTypeID       MDOperatorSSDMDisplace::getOutputTypeID(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
	return RCGlobal::k_SimpleTypeID_OSTexture2D;
}

RCGPUDATAFORMAT     MDOperatorSSDMDisplace::getOutputTexFmt(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
	return RGDF_RGBA16F;
}

DResult             MDOperatorSSDMDisplace::calcOutputTexSize(Int i_Index, MDEffect* i_pMDEffect, DVector2i& o_Size, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
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
MDOperandPtr        MDOperatorSSDMDisplace::execute(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, IRP* o_pInputReleasePoint) const
{
	DOME_ASSERT(i_ParamCount == s_InputCount);
	DOME_ASSERT(i_pParamList[0] && i_pParamList[0]->isTexture());
	DOME_ASSERT(i_pParamList[1] && i_pParamList[1]->isTexture());

	DResult l_Result;
	RCEffectManager* l_pRCEffectMgr = i_pMDEffect->getRCEffect()->getEffectManager();
	RCRenderer* l_pRenderer = l_pRCEffectMgr->getRenderer();

	o_pInputReleasePoint[0] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[1] = IRP_AFTEREXECUTE;

	DVector2i l_RtSize;
	l_Result = calcOutputTexSize(0, i_pMDEffect, l_RtSize, i_ParamCount, i_pParamList);
	DOME_ASSERT(DM_SUCC(l_Result));

	RCGPUDATAFORMAT l_Format = getOutputTexFmt(0, i_pMDEffect, i_ParamCount, i_pParamList);
	DOME_ASSERT(l_Format == RGDF_RGBA8 || l_Format == RGDF_RGBA16F);

	OSTexture2D l_RtTex;
	l_Result = l_pRenderer->createTexture2D(l_RtTex, l_RtSize.x, l_RtSize.y, 1, l_Format, RBU_DEFAULT, DM_TRUE, DM_NULL);
	DOME_ASSERT(DM_SUCC(l_Result));

	RCMOD_Texture l_ColorTexture;
	*((OSTexture2D*)l_ColorTexture.getPtr()) = l_RtTex;

#define GetTexture(name, index) \
	RCMOD_Texture l_##name; \
	DSimpleTypedValue* l_p##name = i_pParamList[index]->getDataPtr();\
	*((OSTexture2D*)l_##name.getPtr()) = l_p##name->getValue<OSTexture2D>();

	GetTexture(Normal, 0);
	GetTexture(SSDM, 1);
	

	RCMOD_Texture* l_OutputTextures[] = {
		&l_ColorTexture,
	};

	RCMOD_Texture* l_InputTextures[] = {
		&l_Normal,
		&l_SSDM,
	};

	// render
	const static DStringHash k_KEY_ScenePlugin("RCPI_Scene");
	RCPI_Scene* l_pScenePlugin = (RCPI_Scene*)l_pRCEffectMgr->getPlugin(k_KEY_ScenePlugin);

	l_pScenePlugin->RenderSSDMDisplace(
		(RCOSRendererData*)l_pRenderer->getOSRendererData(),
		l_InputTextures,
		sizeof(l_InputTextures) / sizeof(l_InputTextures[0]),
		l_OutputTextures,
		sizeof(l_OutputTextures) / sizeof(l_OutputTextures[0]));

	MDOperandValue* l_pRtOperand = DOME_New(MDOperandValue)(RCGlobal::k_SimpleTypeID_OSTexture2D);
	l_Result = l_pRtOperand->getDataPtr()->set(RCGlobal::k_SimpleTypeID_OSTexture2D, &l_RtTex);
	DOME_ASSERT(DM_SUCC(l_Result));

	return l_pRtOperand;
}

DResult             MDOperatorSSDMDisplace::destroyResult(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, MDOperandPtr i_pResult) const
{
	RCRenderer* l_pRenderer = i_pMDEffect->getRCEffect()->getEffectManager()->getRenderer();

	OSTexture2D l_hTexture;
	l_hTexture = *i_pResult->getTexturePtr();
	l_pRenderer->destroyTexture2D(l_hTexture);
	DOME_Del(i_pResult);

	return R_SUCCESS;
}


RC_NAMESPACE_END