#include "pch.h"
/*
    filename:       mdoperatorgpucopyto.cpp
    author:         Ming Dong
    date:           2016-JUN-28
    description:    
*/

#include "mdoperatorgpucopyto.h"
#include <rc/public/iexecuter.h>

#ifdef RC_PERF
#include "KG3D_FrameTimer.h"
#else
#define FRAMETIMER_BEGIN(a,b)
#define FRAMETIMER_END(a)
#endif


RC_NAMESPACE_BEGIN

MDOperatorGpuCopyTo::MDOperatorGpuCopyTo()
    : m_OperatorName("MDGpuCopyTo")
{

}

MDOperatorGpuCopyTo::~MDOperatorGpuCopyTo()
{

}

/****************************
    FROM MDOperator class
****************************/
const DString&      MDOperatorGpuCopyTo::getOperatorName() const
{
    return m_OperatorName;
}

Bool                MDOperatorGpuCopyTo::isGpuOperator() const
{
    return DM_FALSE;
}

Int                 MDOperatorGpuCopyTo::getInputCount() const
{
    return 2;
}

DSimpleTypeID       MDOperatorGpuCopyTo::getInputTypeID(Int i_Index) const
{
    return RCGlobal::k_SimpleTypeID_OSTexture2D;
}

Int                 MDOperatorGpuCopyTo::getOutputCount() const
{
    return 1;
}

DSimpleTypeID       MDOperatorGpuCopyTo::getOutputTypeID(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_Index == 0);
    DOME_ASSERT(i_ParamCount == 2);
    DOME_ASSERT(i_pParamList[0]->getDataCount() == 1);
    return i_pParamList[0]->getDataType(0);
}

RCGPUDATAFORMAT     MDOperatorGpuCopyTo::getOutputTexFmt(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_Index == 0);
    DOME_ASSERT(i_ParamCount == 2);
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

DResult             MDOperatorGpuCopyTo::calcOutputTexSize(Int i_Index, MDEffect* i_pMDEffect, DVector2i& o_Size, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_Index == 0);
    DOME_ASSERT(i_ParamCount == 2);
    DOME_ASSERT(i_pParamList[0]->isTexture());      // render target texture

    return i_pParamList[0]->getTextureSize(o_Size);
}

/****************************
    FROM MDOperatorCpu class
****************************/
MDOperandPtr        MDOperatorGpuCopyTo::execute(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, IRP* o_pInputReleasePoint) const
{
	FRAMETIMER_BEGIN(FTT_RC_CAL_GPUCOPYTO, FTT_RC_CAL_EXECUTE_EFFECTPASS);
    DOME_ASSERT(i_ParamCount == 2);
    DOME_ASSERT(i_pParamList[0]);      // source value
    DOME_ASSERT(i_pParamList[1]);      // target value
    
    o_pInputReleasePoint[0] = IRP_AFTEREXECUTE;
    o_pInputReleasePoint[1] = IRP_INFINISHCALLBACK;

    DSimpleTypedValue* l_pSrc = i_pParamList[0]->getDataPtr();
    DSimpleTypedValue* l_pDst = i_pParamList[1]->getDataPtr();
    DOME_ASSERT(l_pSrc);
    DOME_ASSERT(l_pDst);
    DOME_ASSERT(l_pSrc->getTypeID() == l_pDst->getTypeID());
    DOME_ASSERT(l_pSrc->getTypeID() == RCGlobal::k_SimpleTypeID_OSTexture2D);

    OSTexture2D l_SrcTex = l_pSrc->getValue<OSTexture2D>();
    OSTexture2D l_DstTex = l_pDst->getValue<OSTexture2D>();
    
    RCEffectManager* l_pEffectMgr = i_pMDEffect->getRCEffect()->getEffectManager();
    RCRenderer* l_pRenderer = l_pEffectMgr->getRenderer();

    l_pRenderer->copyTexture2D(l_DstTex, l_SrcTex);

	FRAMETIMER_END(FTT_RC_CAL_GPUCOPYTO);

    return i_pParamList[1];
}

DResult             MDOperatorGpuCopyTo::destroyResult(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, MDOperandPtr i_pResult) const
{
    return R_SUCCESS;
}


RC_NAMESPACE_END