#include "pch.h"
/*
    filename:       mdoperatorrenderwaterpreto.cpp
    author:         Ming Dong
    date:           2016-JUN-28
    description:    
*/

#include "mdoperatorrenderwaterpreto.h"
#include <rc/public/iexecuter.h>

#ifdef RC_PERF
#include "KG3D_FrameTimer.h"
#else
#define FRAMETIMER_BEGIN(a,b)
#define FRAMETIMER_END(a)
#endif


RC_NAMESPACE_BEGIN

// MDSceneRender($F4)
static unsigned int nParamCount = 3;
MDOperatorRenderWaterPreTo::MDOperatorRenderWaterPreTo()
    : m_OperatorName("MDRenderWaterPreTo")
{

}

MDOperatorRenderWaterPreTo::~MDOperatorRenderWaterPreTo()
{

}

/****************************
    FROM MDOperator class
****************************/
const DString&      MDOperatorRenderWaterPreTo::getOperatorName() const
{
    return m_OperatorName;
}

Bool                MDOperatorRenderWaterPreTo::isGpuOperator() const
{
    return DM_FALSE;
}

Int                 MDOperatorRenderWaterPreTo::getInputCount() const
{
    return nParamCount;
}

DSimpleTypeID       MDOperatorRenderWaterPreTo::getInputTypeID(Int i_Index) const
{
    return RCGlobal::k_SimpleTypeID_OSTexture2D;
}

Int                 MDOperatorRenderWaterPreTo::getOutputCount() const
{
    return 2;
}

DSimpleTypeID       MDOperatorRenderWaterPreTo::getOutputTypeID(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_ParamCount == nParamCount);
    return RCGlobal::k_SimpleTypeID_OSTexture2D;
}

RCGPUDATAFORMAT     MDOperatorRenderWaterPreTo::getOutputTexFmt(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
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
    else if (i_Index == 1)
    {
	    l_Result = i_pParamList[1]->getTextureFormat(l_Format);
	    DOME_ASSERT(DM_SUCC(l_Result));
	    return l_Format;
    }
    else
        return RGDF_UNKNOWN;
}

DResult             MDOperatorRenderWaterPreTo::calcOutputTexSize(Int i_Index, MDEffect* i_pMDEffect, DVector2i& o_Size, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_ParamCount == nParamCount);
    DOME_ASSERT(i_pParamList[0]	&& i_pParamList[0]->isTexture());
	DOME_ASSERT(i_pParamList[1] && i_pParamList[1]->isTexture());
    DOME_ASSERT(i_pParamList[2] && i_pParamList[2]->isTexture());

    return i_pParamList[0]->getTextureSize(o_Size);
    if (i_Index == 0)
    {
	    return i_pParamList[0]->getTextureSize(o_Size);
    }
    else if (i_Index == 1)
    {
	    return i_pParamList[1]->getTextureSize(o_Size);
    }
    else
        return R_FAILED;
}

/****************************
    FROM MDOperatorCpu class
****************************/
MDOperandPtr        MDOperatorRenderWaterPreTo::execute(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, IRP* o_pInputReleasePoint) const
{
	FRAMETIMER_BEGIN(FTT_RC_CAL_RENDERWATERPREPARE, FTT_RC_CAL_EXECUTE_EFFECTPASS);
	DOME_ASSERT(i_ParamCount == nParamCount);
	DOME_ASSERT(i_pParamList[0] && i_pParamList[0]->isTexture());
	DOME_ASSERT(i_pParamList[1] && i_pParamList[1]->isTexture());
	DOME_ASSERT(i_pParamList[2] && i_pParamList[2]->isTexture());
    
    RCEffectManager* l_pRCEffectMgr = i_pMDEffect->getRCEffect()->getEffectManager();
    RCRenderer* l_pRenderer = l_pRCEffectMgr->getRenderer();

    o_pInputReleasePoint[0] = IRP_INFINISHCALLBACK;
    o_pInputReleasePoint[1] = IRP_INFINISHCALLBACK;
    o_pInputReleasePoint[2] = IRP_AFTEREXECUTE;

	DSimpleTypedValue* l_pColorValue = i_pParamList[0]->getDataPtr();
    DSimpleTypedValue* l_pDepthValue = i_pParamList[1]->getDataPtr();
    DSimpleTypedValue* l_pNormalValue = i_pParamList[2]->getDataPtr();

	RCMOD_Texture l_ColorTexture;
	*((OSTexture2D*)l_ColorTexture.getPtr()) = l_pColorValue->getValue<OSTexture2D>();

	RCMOD_Texture l_DepthTexture;
	*((OSTexture2D*)l_DepthTexture.getPtr()) = l_pDepthValue->getValue<OSTexture2D>();

  	RCMOD_Texture l_NormalTexture;
	*((OSTexture2D*)l_NormalTexture.getPtr()) = l_pNormalValue->getValue<OSTexture2D>();

    // render
    const static DStringHash k_KEY_ScenePlugin("RCPI_Scene");
    RCPI_Scene* l_pScenePlugin = (RCPI_Scene*)l_pRCEffectMgr->getPlugin(k_KEY_ScenePlugin);


//	l_pScenePlugin->RenderWaterPrepare((RCOSRendererData*)l_pRenderer->getOSRendererData(), &l_ColorTexture, &l_DepthTexture, &l_NormalTexture);
	
	MDOperandArray* l_pOperand = DOME_New(MDOperandArray);
	l_pOperand->addOperand(i_pParamList[0]);
	l_pOperand->addOperand(i_pParamList[1]);

	FRAMETIMER_END(FTT_RC_CAL_RENDERWATERPREPARE);

    return l_pOperand; 
}

DResult             MDOperatorRenderWaterPreTo::destroyResult(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, MDOperandPtr i_pResult) const
{
	DOME_Del(i_pResult);
    return R_SUCCESS;
}


RC_NAMESPACE_END