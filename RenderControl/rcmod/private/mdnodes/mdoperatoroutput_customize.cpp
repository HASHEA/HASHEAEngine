#include "pch.h"
/*
    filename:       mdoperatorscenerender.cpp
    author:         Ming Dong
    date:           2016-JUN-28
    description:    
*/

#include "mdoperatoroutput_customize.h"
#include <rc/public/iexecuter.h>
#include "../../../DevEnv/Include/PerfAnalyzer.h"

#ifdef RC_PERF
#include "KG3D_FrameTimer.h"
#else
#define FRAMETIMER_BEGIN(a,b)
#define FRAMETIMER_END(a)
#endif


RC_NAMESPACE_BEGIN

// MDSceneRender($F4)
static unsigned int nParamCount = 7;
MDOperatorOutput_Customize::MDOperatorOutput_Customize()
    : m_OperatorName("MDOutput_Customize")
{

}

MDOperatorOutput_Customize::~MDOperatorOutput_Customize()
{

}

/****************************
    FROM MDOperator class
****************************/
const DString&      MDOperatorOutput_Customize::getOperatorName() const
{
    return m_OperatorName;
}

Bool                MDOperatorOutput_Customize::isGpuOperator() const
{
    return DM_FALSE;
}

Int                 MDOperatorOutput_Customize::getInputCount() const
{
    return nParamCount;
}

DSimpleTypeID       MDOperatorOutput_Customize::getInputTypeID(Int i_Index) const
{
    switch (i_Index)
    {
    case 0:
    case 1:
        return RCGlobal::k_SimpleTypeID_OSTexture2D;
    case 2:
    case 3:
    case 4:
    case 5:
        return RCGlobal::k_SimpleTypeID_DVector2f;
    case 6:
        return RCGlobal::k_SimpleTypeID_F32;
    }
    return RCGlobal::k_SimpleTypeID_Unknown;
}

Int                 MDOperatorOutput_Customize::getOutputCount() const
{
    return 1;
}

DSimpleTypeID       MDOperatorOutput_Customize::getOutputTypeID(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_ParamCount == nParamCount);
    return RCGlobal::k_SimpleTypeID_OSTexture2D;
}

RCGPUDATAFORMAT     MDOperatorOutput_Customize::getOutputTexFmt(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
	DOME_ASSERT(i_ParamCount == nParamCount);
	DOME_ASSERT(i_pParamList[0] && i_pParamList[0]->isTexture());

	RCGPUDATAFORMAT l_Format;
    DResult l_Result;
    if (i_Index == 0)
    {
	    l_Result = i_pParamList[0]->getTextureFormat(l_Format);
	    DOME_ASSERT(DM_SUCC(l_Result));
	    return l_Format;
    }
    else
        return RGDF_UNKNOWN;
}

DResult             MDOperatorOutput_Customize::calcOutputTexSize(Int i_Index, MDEffect* i_pMDEffect, DVector2i& o_Size, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_ParamCount == nParamCount);

    return i_pParamList[0]->getTextureSize(o_Size);
}

/****************************
    FROM MDOperatorCpu class
****************************/
MDOperandPtr        MDOperatorOutput_Customize::execute(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, IRP* o_pInputReleasePoint) const
{
	PERF_COUNTER_EX(1);
	FRAMETIMER_BEGIN(FTT_RC_CAL_SCENERENDERTO, FTT_RC_CAL_EXECUTE_EFFECTPASS);
	DOME_ASSERT(i_ParamCount == nParamCount);
	DOME_ASSERT(i_pParamList[0] && i_pParamList[0]->isTexture());			// source texture
	DOME_ASSERT(i_pParamList[1] && i_pParamList[1]->isTexture());			// render target texture
	DOME_ASSERT(i_pParamList[2] && i_pParamList[2]->isFloat2());			// source uv pos
	DOME_ASSERT(i_pParamList[3] && i_pParamList[3]->isFloat2());			// source uv size
	DOME_ASSERT(i_pParamList[4] && i_pParamList[4]->isFloat2());			// target uv pos
	DOME_ASSERT(i_pParamList[5] && i_pParamList[5]->isFloat2());			// target uv size
	DOME_ASSERT(i_pParamList[6] && i_pParamList[6]->isFloat());				// blend type
    
    RCEffectManager* l_pRCEffectMgr = i_pMDEffect->getRCEffect()->getEffectManager();
    RCRenderer* l_pRenderer = l_pRCEffectMgr->getRenderer();

    o_pInputReleasePoint[0] = IRP_AFTEREXECUTE;
    o_pInputReleasePoint[1] = IRP_INFINISHCALLBACK;
    o_pInputReleasePoint[2] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[3] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[4] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[5] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[6] = IRP_AFTEREXECUTE;

	DSimpleTypedValue* l_pColorValue		= i_pParamList[0]->getDataPtr();
    DSimpleTypedValue* l_pOutRTValue		= i_pParamList[1]->getDataPtr();
    DSimpleTypedValue* l_pSrcUVPosValue		= i_pParamList[2]->getDataPtr();
	DSimpleTypedValue* l_pSrcUVSizeValue	= i_pParamList[3]->getDataPtr();
	DSimpleTypedValue* l_pDstUVPosValue	    = i_pParamList[4]->getDataPtr();
	DSimpleTypedValue* l_pDstUVSizeValue	= i_pParamList[5]->getDataPtr();
	DSimpleTypedValue* l_pBlendTypeValue	= i_pParamList[6]->getDataPtr();

	RCMOD_Texture l_ColorTexture;
	*((OSTexture2D*)l_ColorTexture.getPtr()) = l_pColorValue->getValue<OSTexture2D>();

	RCMOD_Texture l_OutRTTexture;
	*((OSTexture2D*)l_OutRTTexture.getPtr()) = l_pOutRTValue->getValue<OSTexture2D>();

    DVector2f l_SrcUVPos = l_pSrcUVPosValue->getDVector2f();
    RCMOD_Float2 l_SrcUVPosParam(l_SrcUVPos.x, l_SrcUVPos.y);

    DVector2f l_SrcUVSize = l_pSrcUVSizeValue->getDVector2f();
    RCMOD_Float2 l_SrcUVSizeParam(l_SrcUVSize.x, l_SrcUVSize.y);

    DVector2f l_DstUVPos = l_pDstUVPosValue->getDVector2f();
    RCMOD_Float2 l_DstUVPosParam(l_DstUVPos.x, l_DstUVPos.y);

    DVector2f l_DstUVSize = l_pDstUVSizeValue->getDVector2f();
    RCMOD_Float2 l_DstUVSizeParam(l_DstUVSize.x, l_DstUVSize.y);

    float l_BlendTypeParam = l_pBlendTypeValue->getF32();

    // render
    const static DStringHash k_KEY_ScenePlugin("RCPI_Scene");
    RCPI_Scene* l_pScenePlugin = (RCPI_Scene*)l_pRCEffectMgr->getPlugin(k_KEY_ScenePlugin);


	l_pScenePlugin->CustomizeOutput((RCOSRendererData*)l_pRenderer->getOSRendererData(), &l_ColorTexture, &l_OutRTTexture, l_SrcUVPosParam, l_SrcUVSizeParam, l_DstUVPosParam, l_DstUVSizeParam, l_BlendTypeParam);
	
	FRAMETIMER_END(FTT_RC_CAL_SCENERENDERTO);

    return i_pParamList[1]; 
}

DResult             MDOperatorOutput_Customize::destroyResult(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, MDOperandPtr i_pResult) const
{
    return R_SUCCESS;
}


RC_NAMESPACE_END