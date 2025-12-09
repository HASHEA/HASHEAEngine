#include "pch.h"
/*
    filename:       mdoperatorscenerender.cpp
    author:         Ming Dong
    date:           2016-JUN-28
    description:    
*/

#include "MDOperatorOITTransparent.h"
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
static unsigned int nParamCount = 6;
MDOperatorOITTransparent::MDOperatorOITTransparent()
    : m_OperatorName("MDOITTransparent")
{

}

MDOperatorOITTransparent::~MDOperatorOITTransparent()
{

}

/****************************
    FROM MDOperator class
****************************/
const DString&      MDOperatorOITTransparent::getOperatorName() const
{
    return m_OperatorName;
}

Bool                MDOperatorOITTransparent::isGpuOperator() const
{
    return DM_FALSE;
}

Int                 MDOperatorOITTransparent::getInputCount() const
{
    return nParamCount;
}

DSimpleTypeID       MDOperatorOITTransparent::getInputTypeID(Int i_Index) const
{
	DSimpleTypeID l_ParamTypeID;

	switch (i_Index)
	{
	case 0:
	case 1:
	case 2:
	case 3:
		l_ParamTypeID = RCGlobal::k_SimpleTypeID_OSTexture2D;
		break;
	case 5:
		l_ParamTypeID = RCGlobal::k_SimpleTypeID_F32;
		break;
	case 4:
	default:
		l_ParamTypeID = RCGlobal::k_SimpleTypeID_DVector4f;
		break;
	}

	return l_ParamTypeID;
}

Int                 MDOperatorOITTransparent::getOutputCount() const
{
    return 3;
}

DSimpleTypeID       MDOperatorOITTransparent::getOutputTypeID(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_ParamCount == nParamCount);

    return RCGlobal::k_SimpleTypeID_OSTexture2D;;
}

RCGPUDATAFORMAT     MDOperatorOITTransparent::getOutputTexFmt(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
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
	else if (i_Index == 2)
	{
		l_Result = i_pParamList[2]->getTextureFormat(l_Format);
		DOME_ASSERT(DM_SUCC(l_Result));
		return l_Format;
	}
    else
        return RGDF_UNKNOWN;
}

DResult             MDOperatorOITTransparent::calcOutputTexSize(Int i_Index, MDEffect* i_pMDEffect, DVector2i& o_Size, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_ParamCount == nParamCount);
    DOME_ASSERT(i_pParamList[0]	&& i_pParamList[0]->isTexture());
	DOME_ASSERT(i_pParamList[1] && i_pParamList[1]->isTexture());
    DOME_ASSERT(i_pParamList[2] && i_pParamList[2]->isTexture());
	DOME_ASSERT(i_pParamList[3] && i_pParamList[3]->isTexture());
	DOME_ASSERT(i_pParamList[4] && i_pParamList[4]->isFloat4());
	DOME_ASSERT(i_pParamList[5] && i_pParamList[5]->isFloat());

    return i_pParamList[0]->getTextureSize(o_Size);
    if (i_Index == 0)
    {
	    return i_pParamList[0]->getTextureSize(o_Size);
    }
    else if (i_Index == 1)
    {
	    return i_pParamList[1]->getTextureSize(o_Size);
    }
	else if (i_Index == 2)
	{
		return i_pParamList[2]->getTextureSize(o_Size);
	}
    else
        return R_FAILED;
}

/****************************
    FROM MDOperatorCpu class
****************************/
MDOperandPtr        MDOperatorOITTransparent::execute(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, IRP* o_pInputReleasePoint) const
{
	PERF_COUNTER_EX(0);
	FRAMETIMER_BEGIN(FTT_RC_CAL_OIT, FTT_RC_CAL_EXECUTE_EFFECTPASS);
	DOME_ASSERT(i_ParamCount == nParamCount);
	DOME_ASSERT(i_pParamList[0] && i_pParamList[0]->isTexture());           //color0
	DOME_ASSERT(i_pParamList[1] && i_pParamList[1]->isTexture());           //color1
	DOME_ASSERT(i_pParamList[2] && i_pParamList[2]->isTexture());           //depth
    DOME_ASSERT(i_pParamList[3] && i_pParamList[3]->isTexture());            // oit opaque depth
	DOME_ASSERT(i_pParamList[4] && i_pParamList[4]->isFloat4());            //view
	DOME_ASSERT(i_pParamList[5] && i_pParamList[5]->isFloat());				// render queue

    RCEffectManager* l_pRCEffectMgr = i_pMDEffect->getRCEffect()->getEffectManager();
    RCRenderer* l_pRenderer = l_pRCEffectMgr->getRenderer();

    o_pInputReleasePoint[0] = IRP_INFINISHCALLBACK;
    o_pInputReleasePoint[1] = IRP_INFINISHCALLBACK;
    o_pInputReleasePoint[2] = IRP_INFINISHCALLBACK;
	o_pInputReleasePoint[3] = IRP_INFINISHCALLBACK;
    o_pInputReleasePoint[4] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[5] = IRP_AFTEREXECUTE;

	DSimpleTypedValue* l_pColorValue0 = i_pParamList[0]->getDataPtr();
    DSimpleTypedValue* l_pColorValue1 = i_pParamList[1]->getDataPtr();
    DSimpleTypedValue* l_pDepthValue = i_pParamList[2]->getDataPtr();
	DSimpleTypedValue* l_pOitOpaqueDepthValue = i_pParamList[3]->getDataPtr();
    DSimpleTypedValue* l_pViewport = i_pParamList[4]->getDataPtr();
	DSimpleTypedValue* l_pRenderQueue = i_pParamList[5]->getDataPtr();

    DVector4f l_Viewport = l_pViewport->getDVector4f();
	F32 l_nRenderQueue = l_pRenderQueue->getF32();
    DVector2i l_RtSize;
    calcOutputTexSize(0, i_pMDEffect, l_RtSize, i_ParamCount, i_pParamList);
    RCMOD_Float4 l_ViewPort(l_Viewport.x * l_RtSize.x, l_Viewport.y * l_RtSize.y, l_Viewport.z * l_RtSize.x, l_Viewport.w * l_RtSize.y);

	RCMOD_Texture l_ColorTexture0;
	*((OSTexture2D*)l_ColorTexture0.getPtr()) = l_pColorValue0->getValue<OSTexture2D>();

    RCMOD_Texture l_ColorTexture1;
    *((OSTexture2D*)l_ColorTexture1.getPtr()) = l_pColorValue1->getValue<OSTexture2D>();

	RCMOD_Texture l_DepthTexture;
	*((OSTexture2D*)l_DepthTexture.getPtr()) = l_pDepthValue->getValue<OSTexture2D>();

	RCMOD_Texture l_OITOpaqueDepthTexture;
	*((OSTexture2D*)l_OITOpaqueDepthTexture.getPtr()) = l_pOitOpaqueDepthValue->getValue<OSTexture2D>();

    // render
    const static DStringHash k_KEY_ScenePlugin("RCPI_Scene");
    RCPI_Scene* l_pScenePlugin = (RCPI_Scene*)l_pRCEffectMgr->getPlugin(k_KEY_ScenePlugin);


	l_pScenePlugin->RenderOITTransparent((RCOSRendererData*)l_pRenderer->getOSRendererData(), &l_ColorTexture0, &l_ColorTexture1, &l_DepthTexture, &l_OITOpaqueDepthTexture, l_ViewPort, l_nRenderQueue);
	
	MDOperandArray* l_pOperand = DOME_New(MDOperandArray);
	l_pOperand->addOperand(i_pParamList[0]);
	l_pOperand->addOperand(i_pParamList[1]);
	l_pOperand->addOperand(i_pParamList[2]);

	FRAMETIMER_END(FTT_RC_CAL_OIT);

    return l_pOperand; 
}

DResult             MDOperatorOITTransparent::destroyResult(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, MDOperandPtr i_pResult) const
{
	DOME_Del(i_pResult);
    return R_SUCCESS;
}


RC_NAMESPACE_END