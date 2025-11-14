#include "pch.h"
/*
    filename:       mdoperatorscenerender.cpp
    author:         Ming Dong
    date:           2016-JUN-28
    description:    
*/

#include "mdoperatormoon.h"
#include <rc/public/iexecuter.h>

#ifdef RC_PERF
#include "KG3D_FrameTimer.h"
#else
#define FRAMETIMER_BEGIN(a,b)
#define FRAMETIMER_END(a)
#endif


RC_NAMESPACE_BEGIN

// MDSceneRender($F4)
static const unsigned int nParamCount = 5;
MDOperatorMoon::MDOperatorMoon()
    : m_OperatorName("MDMoon")
{

}

MDOperatorMoon::~MDOperatorMoon() 
{

}

/****************************
    FROM MDOperator class
****************************/
const DString&      MDOperatorMoon::getOperatorName() const
{
    return m_OperatorName;
}

Bool                MDOperatorMoon::isGpuOperator() const
{
    return DM_FALSE;
}

Int                 MDOperatorMoon::getInputCount() const
{
    return nParamCount;
}

DSimpleTypeID       MDOperatorMoon::getInputTypeID(Int i_Index) const
{
	DOME_ASSERT(i_Index < 0 || i_Index >= nParamCount);
	DSimpleTypeID InputTypes[nParamCount] = {
		RCGlobal::k_SimpleTypeID_OSTexture2D,		//Depth Input		
		RCGlobal::k_SimpleTypeID_OSTexture2D,
		RCGlobal::k_SimpleTypeID_OSTexture2D,
		RCGlobal::k_SimpleTypeID_DVector4f,
		RCGlobal::k_SimpleTypeID_DVector4f,
	};
	return InputTypes[i_Index];
}

Int                 MDOperatorMoon::getOutputCount() const
{
    return 1;
}

DSimpleTypeID       MDOperatorMoon::getOutputTypeID(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_ParamCount == nParamCount);
    return RCGlobal::k_SimpleTypeID_OSTexture2D;
}

RCGPUDATAFORMAT     MDOperatorMoon::getOutputTexFmt(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
	DOME_ASSERT(i_ParamCount == nParamCount);
	DOME_ASSERT(i_pParamList[0] && i_pParamList[0]->isTexture());

	return RGDF_RGBA16F;
}

DResult             MDOperatorMoon::calcOutputTexSize(Int i_Index, MDEffect* i_pMDEffect, DVector2i& o_Size, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
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
MDOperandPtr        MDOperatorMoon::execute(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, IRP* o_pInputReleasePoint) const
{
	FRAMETIMER_BEGIN(FTT_RC_CAL_SCENERENDERTO, FTT_RC_CAL_EXECUTE_EFFECTPASS);
	DOME_ASSERT(i_ParamCount == nParamCount);
	DOME_ASSERT(i_pParamList[0] && i_pParamList[0]->isTexture());
	DOME_ASSERT(i_pParamList[1] && i_pParamList[1]->isTexture());
	DOME_ASSERT(i_pParamList[2] && i_pParamList[2]->isTexture());
	DOME_ASSERT(i_pParamList[3] && i_pParamList[3]->isFloat4());
	DOME_ASSERT(i_pParamList[4] && i_pParamList[4]->isFloat4());
	
	RCEffectManager* l_pRCEffectMgr = i_pMDEffect->getRCEffect()->getEffectManager();
	RCRenderer* l_pRenderer = l_pRCEffectMgr->getRenderer();

	o_pInputReleasePoint[0] = IRP_INFINISHCALLBACK;
	o_pInputReleasePoint[1] = IRP_AFTEREXECUTE;	
	o_pInputReleasePoint[2] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[3] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[4] = IRP_AFTEREXECUTE;

//	DResult l_Result;

	DSimpleTypedValue* l_pColorOutputValue = i_pParamList[0]->getDataPtr();	
	RCMOD_Texture l_ColorOutputTexture;
	*((OSTexture2D*)l_ColorOutputTexture.getPtr()) = l_pColorOutputValue->getValue<OSTexture2D>();

	DSimpleTypedValue* l_pDepthInputValue = i_pParamList[1]->getDataPtr();
	RCMOD_Texture l_DepthInputTexture;
	*((OSTexture2D*)l_DepthInputTexture.getPtr()) = l_pDepthInputValue->getValue<OSTexture2D>();

	DSimpleTypedValue* l_pMoonValue = i_pParamList[2]->getDataPtr();
	RCMOD_Texture l_MoonTexture;
	*((OSTexture2D*)l_MoonTexture.getPtr()) = l_pMoonValue->getValue<OSTexture2D>();

	DSimpleTypedValue* l_pf3Param = i_pParamList[3]->getDataPtr();
	RCMOD_Float4 l_Param;
	*(DVector4f*)l_Param.getPtr() = (l_pf3Param->getDVector4f());

	DSimpleTypedValue* l_pMoonColor = i_pParamList[4]->getDataPtr();
	RCMOD_Float4 l_MoonColor;
	*(DVector4f*)l_MoonColor.getPtr() = (l_pMoonColor->getDVector4f());

	// render
	const static DStringHash k_KEY_ScenePlugin("RCPI_Scene");
	RCPI_Scene* l_pScenePlugin = (RCPI_Scene*)l_pRCEffectMgr->getPlugin(k_KEY_ScenePlugin);

	l_pScenePlugin->RenderRealSkyMoon((RCOSRendererData*)l_pRenderer->getOSRendererData(), &l_ColorOutputTexture, &l_DepthInputTexture, &l_MoonTexture, l_Param, l_MoonColor);

	MDOperandValue* l_pRtOperand = DOME_New(MDOperandValue)(RCGlobal::k_SimpleTypeID_OSTexture2D);
	l_pRtOperand->getDataPtr()->set(RCGlobal::k_SimpleTypeID_OSTexture2D, l_ColorOutputTexture.getPtr());

	FRAMETIMER_END(FTT_RC_CAL_SCENERENDERTO);

	return l_pRtOperand;
}

DResult             MDOperatorMoon::destroyResult(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, MDOperandPtr i_pResult) const
{
	return R_SUCCESS;
}


RC_NAMESPACE_END