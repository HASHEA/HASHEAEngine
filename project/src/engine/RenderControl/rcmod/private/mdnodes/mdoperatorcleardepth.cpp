#include "pch.h"
/*
    filename:       mdoperatorcleardepth.cpp
    author:         Ming Dong
    date:           2016-DEC-01
    description:    
*/

#include "mdoperatorcleardepth.h"
#include <rc/public/iexecuter.h>

#ifdef RC_PERF
#include "KG3D_FrameTimer.h"
#else
#define FRAMETIMER_BEGIN(a,b)
#define FRAMETIMER_END(a)
#endif


RC_NAMESPACE_BEGIN

MDOperatorClearDepth::MDOperatorClearDepth()
    : m_OperatorName("MDClearDepth")
{

}

MDOperatorClearDepth::~MDOperatorClearDepth()
{

}

/****************************
    FROM MDOperator class
****************************/
const DString&      MDOperatorClearDepth::getOperatorName() const
{
    return m_OperatorName;
}

Bool                MDOperatorClearDepth::isGpuOperator() const
{
    return DM_FALSE;
}

Int                 MDOperatorClearDepth::getInputCount() const
{
    return 3;
}

DSimpleTypeID       MDOperatorClearDepth::getInputTypeID(Int i_Index) const
{
    switch (i_Index)
    {
    case 0:
        return RCGlobal::k_SimpleTypeID_OSTexture2D;
    case 1:
    case 2:
        return RCGlobal::k_SimpleTypeID_F32;
    }
    return RCGlobal::k_SimpleTypeID_Unknown;
}

Int                 MDOperatorClearDepth::getOutputCount() const
{
    return 1;
}

DSimpleTypeID       MDOperatorClearDepth::getOutputTypeID(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_Index == 0);
    return RCGlobal::k_SimpleTypeID_OSTexture2D;
}

RCGPUDATAFORMAT     MDOperatorClearDepth::getOutputTexFmt(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_Index == 0);
    DOME_ASSERT(i_ParamCount == 3);
    DOME_ASSERT(i_pParamList[0] && i_pParamList[0]->isTexture());

    RCGPUDATAFORMAT l_Format;
    DResult l_Result = i_pParamList[0]->getTextureFormat(l_Format);
    DOME_ASSERT(DM_SUCC(l_Result));
    return l_Format;
}

DResult             MDOperatorClearDepth::calcOutputTexSize(Int i_Index, MDEffect* i_pMDEffect, DVector2i& o_Size, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_Index == 0);
    DOME_ASSERT(i_ParamCount == 3);
    DOME_ASSERT(i_pParamList[0] && i_pParamList[0]->isTexture());

    return i_pParamList[0]->getTextureSize(o_Size);
}

/****************************
    FROM MDOperatorCpu class
****************************/
MDOperandPtr        MDOperatorClearDepth::execute(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, IRP* o_pInputReleasePoint) const
{
    PERF_COUNTER_EX(0);
    DResult l_Result;
    DOME_ASSERT(i_ParamCount == 3);
    DOME_ASSERT(i_pParamList[0]->isTexture());      // source texture
    DOME_ASSERT(i_pParamList[1]->isFloat());
    DOME_ASSERT(i_pParamList[2]->isFloat());

    RCEffectManager* l_pEffectMgr = i_pMDEffect->getRCEffect()->getEffectManager();
    RCRenderer* l_pRenderer = l_pEffectMgr->getRenderer();

    o_pInputReleasePoint[0] = IRP_INFINISHCALLBACK;
    o_pInputReleasePoint[1] = IRP_AFTEREXECUTE;
    o_pInputReleasePoint[2] = IRP_AFTEREXECUTE;

    OSTexture2D l_SrcTex = *i_pParamList[0]->getTexturePtr();
    RCMOD_Texture l_SourceTexture;
    *((OSTexture2D*)l_SourceTexture.getPtr()) = l_SrcTex;

    float l_fDepth, l_fStencil;

    l_Result = i_pParamList[1]->getFloat(l_fDepth);
    DOME_ASSERT(DM_SUCC(l_Result));
    l_Result = i_pParamList[2]->getFloat(l_fStencil);
    DOME_ASSERT(DM_SUCC(l_Result));

	// render
	const static DStringHash k_KEY_ScenePlugin("RCPI_Scene");
	RCPI_Scene* l_pScenePlugin = (RCPI_Scene*)l_pEffectMgr->getPlugin(k_KEY_ScenePlugin);

    l_pScenePlugin->RCP_ClearDepth((RCOSRendererData*)l_pRenderer->getOSRendererData(), &l_SourceTexture, l_fDepth, l_fStencil);

    MDOperandValue* l_pRtOperand = DOME_New(MDOperandValue)(RCGlobal::k_SimpleTypeID_OSTexture2D);
    l_Result = l_pRtOperand->getDataPtr()->set(RCGlobal::k_SimpleTypeID_OSTexture2D, &l_SrcTex);
    DOME_ASSERT(DM_SUCC(l_Result));

    return l_pRtOperand;
}

DResult             MDOperatorClearDepth::destroyResult(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, MDOperandPtr i_pResult) const
{
	DOME_Del(i_pResult);

    return R_SUCCESS;
}


RC_NAMESPACE_END