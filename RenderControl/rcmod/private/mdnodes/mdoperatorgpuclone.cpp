#include "pch.h"
/*
    filename:       mdoperatorgpuclone.cpp
    author:         Ming Dong
    date:           2016-SEP-23
    description:    
*/

#include "mdoperatorgpuclone.h"
#include <rc/public/iexecuter.h>

#ifdef RC_PERF
#include "KG3D_FrameTimer.h"
#else
#define FRAMETIMER_BEGIN(a,b)
#define FRAMETIMER_END(a)
#endif


RC_NAMESPACE_BEGIN

MDOperatorGpuClone::MDOperatorGpuClone()
    : m_OperatorName("MDGpuClone")
{

}

MDOperatorGpuClone::~MDOperatorGpuClone()
{

}

/****************************
    FROM MDOperator class
****************************/
const DString&      MDOperatorGpuClone::getOperatorName() const
{
    return m_OperatorName;
}

Bool                MDOperatorGpuClone::isGpuOperator() const
{
    return DM_FALSE;
}

Int                 MDOperatorGpuClone::getInputCount() const
{
    return 1;
}

DSimpleTypeID       MDOperatorGpuClone::getInputTypeID(Int i_Index) const
{
    return RCGlobal::k_SimpleTypeID_OSTexture2D;
}

Int                 MDOperatorGpuClone::getOutputCount() const
{
    return 2;
}

DSimpleTypeID       MDOperatorGpuClone::getOutputTypeID(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_ParamCount == 1);
    DOME_ASSERT(i_pParamList[0]->getDataCount() == 1);
    return i_pParamList[0]->getDataType(0);
}

RCGPUDATAFORMAT     MDOperatorGpuClone::getOutputTexFmt(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_ParamCount == 1);
    if (getOutputTypeID(i_Index, i_pMDEffect, i_ParamCount, i_pParamList) == RCGlobal::k_SimpleTypeID_OSTexture2D)
    {
        DOME_ASSERT(i_pParamList[0] && i_pParamList[0]->isTexture());

        RCGPUDATAFORMAT l_Format;
        DResult l_Result = i_pParamList[0]->getTextureFormat(l_Format);
        DOME_ASSERT(DM_SUCC(l_Result));
        return l_Format;
    }
    else
        return RGDF_UNKNOWN;
}

DResult             MDOperatorGpuClone::calcOutputTexSize(Int i_Index, MDEffect* i_pMDEffect, DVector2i& o_Size, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_ParamCount == 1);
    DOME_ASSERT(i_pParamList[0]->isTexture());      // render target texture

    return i_pParamList[0]->getTextureSize(o_Size);
}

/****************************
    FROM MDOperatorCpu class
****************************/
MDOperandPtr        MDOperatorGpuClone::execute(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, IRP* o_pInputReleasePoint) const
{
	FRAMETIMER_BEGIN(FTT_RC_CAL_GPUCLONE, FTT_RC_CAL_EXECUTE_EFFECTPASS);
    DOME_ASSERT(i_ParamCount == 1);
    DOME_ASSERT(i_pParamList[0]);      // source value
    
    o_pInputReleasePoint[0] = IRP_INFINISHCALLBACK;

    DSimpleTypedValue* l_pSrc = i_pParamList[0]->getDataPtr();
    DOME_ASSERT(l_pSrc);
    DOME_ASSERT(l_pSrc->getTypeID() == RCGlobal::k_SimpleTypeID_OSTexture2D);

    OSTexture2D l_SrcTex = l_pSrc->getValue<OSTexture2D>();

    DResult l_Result;
    RCRenderer* l_pRenderer = i_pMDEffect->getRCEffect()->getEffectManager()->getRenderer();
    OSTexture2D l_hDstTex;
    DVector2i l_Size;
    RCGPUDATAFORMAT l_Format;
//    l_Result = calcOutputTexSize(0, i_pMDEffect, l_Size, i_ParamCount, i_pParamList);
//    DOME_ASSERT(DM_SUCC(l_Result));
//    l_Format = getOutputTexFmt(0, i_pMDEffect, i_ParamCount, i_pParamList);

    l_pRenderer->getTexture2DSize(l_SrcTex, l_Size.x, l_Size.y);
    l_Format = l_pRenderer->getTexture2DFormat(l_SrcTex);

    l_Result = l_pRenderer->createTexture2D(l_hDstTex, l_Size.x, l_Size.y, 1, l_Format, RBU_DEFAULT, DM_TRUE, DM_NULL);
    DOME_ASSERT(DM_SUCC(l_Result));

    l_Result = l_pRenderer->copyTexture2D(l_hDstTex, l_SrcTex);
    DOME_ASSERT(DM_SUCC(l_Result));

    MDOperandValue* l_pRtOperand = DOME_New(MDOperandValue)(RCGlobal::k_SimpleTypeID_OSTexture2D);
    l_Result = l_pRtOperand->getDataPtr()->set(RCGlobal::k_SimpleTypeID_OSTexture2D, &l_hDstTex);
    DOME_ASSERT(DM_SUCC(l_Result));

    MDOperandArray* l_pOperand = DOME_New(MDOperandArray);
    l_pOperand->addOperand(i_pParamList[0]);
    l_pOperand->addOperand(l_pRtOperand);

	FRAMETIMER_END(FTT_RC_CAL_GPUCLONE);
    return l_pOperand;
}

DResult             MDOperatorGpuClone::destroyResult(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, MDOperandPtr i_pResult) const
{
    RCRenderer* l_pRenderer = i_pMDEffect->getRCEffect()->getEffectManager()->getRenderer();

    OSTexture2D l_hTexture;
    MDOperandValue* l_pRtOperand = (MDOperandValue*)i_pResult->getSubOperand(1);
    l_hTexture = *l_pRtOperand->getTexturePtr();
    l_pRenderer->destroyTexture2D(l_hTexture);
    DOME_Del(l_pRtOperand);

    DOME_Del(i_pResult);

    return R_SUCCESS;
}


RC_NAMESPACE_END