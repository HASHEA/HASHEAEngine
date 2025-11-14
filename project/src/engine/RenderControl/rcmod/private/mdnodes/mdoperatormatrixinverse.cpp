#include "pch.h"
/*
    filename:       mdoperatormatrixinverse.cpp
    author:         Ming Dong
    date:           2016-Aug-18
    description:    
*/

#include "mdoperatormatrixinverse.h"
#include <rc/public/iexecuter.h>

#ifdef RC_PERF
#include "KG3D_FrameTimer.h"
#else
#define FRAMETIMER_BEGIN(a,b)
#define FRAMETIMER_END(a)
#endif


RC_NAMESPACE_BEGIN

MDOperatorMatrixInverse::MDOperatorMatrixInverse()
    : m_OperatorName("MDMatrixInverse")
{

}

MDOperatorMatrixInverse::~MDOperatorMatrixInverse()
{

}

/****************************
    FROM MDOperator class
****************************/
const DString&      MDOperatorMatrixInverse::getOperatorName() const
{
    return m_OperatorName;
}

Bool                MDOperatorMatrixInverse::isGpuOperator() const
{
    return DM_FALSE;
}

Int                 MDOperatorMatrixInverse::getInputCount() const
{
    return 1;
}

DSimpleTypeID       MDOperatorMatrixInverse::getInputTypeID(Int i_Index) const
{
    return RCGlobal::k_SimpleTypeID_Unknown;
}

Int                 MDOperatorMatrixInverse::getOutputCount() const
{
    return 1;
}

DSimpleTypeID       MDOperatorMatrixInverse::getOutputTypeID(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_Index == 0);
    DOME_ASSERT(i_ParamCount == 1);
    DOME_ASSERT(i_pParamList[0]->getDataCount() == 1);
    return i_pParamList[0]->getDataType(0);
}

RCGPUDATAFORMAT     MDOperatorMatrixInverse::getOutputTexFmt(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_Index == 0);
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

DResult             MDOperatorMatrixInverse::calcOutputTexSize(Int i_Index, MDEffect* i_pMDEffect, DVector2i& o_Size, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_Index == 0);
    DOME_ASSERT(i_ParamCount == 1);
    DOME_ASSERT(i_pParamList[0]->isTexture());      // render target texture

    return i_pParamList[0]->getTextureSize(o_Size);
}

/****************************
    FROM MDOperatorCpu class
****************************/
MDOperandPtr        MDOperatorMatrixInverse::execute(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, IRP* o_pInputReleasePoint) const
{
	FRAMETIMER_BEGIN(FTT_RC_CAL_MATRIXINVERSE, FTT_RC_CAL_EXECUTE_EFFECTPASS);
    DOME_ASSERT(i_ParamCount == 1);
    DOME_ASSERT(i_pParamList[0]);      // source value
    
    o_pInputReleasePoint[0] = IRP_AFTEREXECUTE;

    DSimpleTypedValue* l_pSrc = i_pParamList[0]->getDataPtr();
    DOME_ASSERT(l_pSrc);

    if (l_pSrc->getTypeID() == RCGlobal::k_SimpleTypeID_DMatrix4x4f)
    {
        DMatrix4x4f l_SrcMat = l_pSrc->getDMatrix4x4f();
        DMatrix4x4f l_ResultMat = l_SrcMat.inverse();
        MDOperandValue* l_pOperand = DOME_New(MDOperandValue)(RCGlobal::k_SimpleTypeID_DMatrix4x4f);
        l_pOperand->getDataPtr()->setDMatrix4x4f(l_ResultMat);
        return l_pOperand;
    }
    DOME_ASSERT(0);

	FRAMETIMER_END(FTT_RC_CAL_MATRIXINVERSE);
    return DM_NULL;
}

DResult             MDOperatorMatrixInverse::destroyResult(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, MDOperandPtr i_pResult) const
{
    if (i_pResult)
    {
        DOME_Del(i_pResult);
    }
    return R_SUCCESS;
}


RC_NAMESPACE_END