#include "pch.h"
/*
    filename:       mdoperatorcopyvalueto.cpp
    author:         Ming Dong
    date:           2016-JUN-28
    description:    
*/

#include "mdoperatorfloatceil.h"
#include <rc/public/iexecuter.h>

#ifdef RC_PERF
#include "KG3D_FrameTimer.h"
#else
#define FRAMETIMER_BEGIN(a,b)
#define FRAMETIMER_END(a)
#endif


RC_NAMESPACE_BEGIN

MDOperatorFloatCeil::MDOperatorFloatCeil()
    : m_OperatorName("MDFloatCeil")
{

}

MDOperatorFloatCeil::~MDOperatorFloatCeil()
{

}

/****************************
    FROM MDOperator class
****************************/
const DString&      MDOperatorFloatCeil::getOperatorName() const
{
    return m_OperatorName;
}

Bool                MDOperatorFloatCeil::isGpuOperator() const
{
    return DM_FALSE;
}

Int                 MDOperatorFloatCeil::getInputCount() const
{
    return 1;
}

DSimpleTypeID       MDOperatorFloatCeil::getInputTypeID(Int i_Index) const
{
    return RCGlobal::k_SimpleTypeID_Unknown;
}

Int                 MDOperatorFloatCeil::getOutputCount() const
{
    return 1;
}

DSimpleTypeID       MDOperatorFloatCeil::getOutputTypeID(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_Index == 0);
    DOME_ASSERT(i_ParamCount == 1);
    DOME_ASSERT(i_pParamList[0]->getDataCount() == 1);
    return i_pParamList[0]->getDataType(0);
}

RCGPUDATAFORMAT     MDOperatorFloatCeil::getOutputTexFmt(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_Index == 0);
    DOME_ASSERT(i_ParamCount == 1);
    
    return RGDF_UNKNOWN;
}

DResult             MDOperatorFloatCeil::calcOutputTexSize(Int i_Index, MDEffect* i_pMDEffect, DVector2i& o_Size, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    return i_pParamList[0]->getTextureSize(o_Size);
}

/****************************
    FROM MDOperatorCpu class
****************************/
MDOperandPtr        MDOperatorFloatCeil::execute(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, IRP* o_pInputReleasePoint) const
{
    PERF_COUNTER_EX(0);
	FRAMETIMER_BEGIN(FTT_RC_CAL_FLOATCEIL, FTT_RC_CAL_EXECUTE_EFFECTPASS);
	DOME_ASSERT(i_ParamCount == 1);
	DOME_ASSERT(i_pParamList[0]);      // source value
	DOME_ASSERT(i_pParamList[0]->getDataCount() == 1);

    o_pInputReleasePoint[0] = IRP_AFTEREXECUTE;

	DSimpleTypeID l_TypeID = i_pParamList[0]->getDataType(0);
	DSimpleTypedValue* l_pSrc = i_pParamList[0]->getDataPtr();   
	DOME_ASSERT(l_pSrc);

	if (l_TypeID == RCGlobal::k_SimpleTypeID_F32)									
	{
		F32 l_fSrc = l_pSrc->getF32();
		F32 l_ResultF32 = ceil(l_fSrc);
		MDOperandValue* l_pOperand = DOME_New(MDOperandValue)(RCGlobal::k_SimpleTypeID_F32);
		l_pOperand->getDataPtr()->setF32(l_ResultF32);
        FRAMETIMER_END(FTT_RC_CAL_FLOATCEIL);
		return l_pOperand;
	}
	else if (l_TypeID == RCGlobal::k_SimpleTypeID_DVector2f)						
	{
		DVector2f l_SrcVec2 = l_pSrc->getDVector2f();
		DVector2f l_ResultVec2 = DVector2f(ceil(l_SrcVec2.x), ceil(l_SrcVec2.y));
		MDOperandValue* l_pOperand = DOME_New(MDOperandValue)(RCGlobal::k_SimpleTypeID_DVector2f);
		l_pOperand->getDataPtr()->setDVector2f(l_ResultVec2);
        FRAMETIMER_END(FTT_RC_CAL_FLOATCEIL);
		return l_pOperand;
	}
	else if (l_TypeID == RCGlobal::k_SimpleTypeID_DVector3f)						
	{
		DVector3f l_SrcVec3 = l_pSrc->getDVector3f();
		DVector3f l_ResultVec3 = DVector3f(ceil(l_SrcVec3.x), ceil(l_SrcVec3.y), ceil(l_SrcVec3.z));
		MDOperandValue* l_pOperand = DOME_New(MDOperandValue)(RCGlobal::k_SimpleTypeID_DVector3f);
		l_pOperand->getDataPtr()->setDVector3f(l_ResultVec3);
		FRAMETIMER_END(FTT_RC_CAL_FLOATCEIL);
        return l_pOperand;
	}
	else if (l_TypeID == RCGlobal::k_SimpleTypeID_DVector4f)						
	{
		DVector4f l_SrcVec4 = l_pSrc->getDVector4f();
		DVector4f l_ResultVec4 = DVector4f(ceil(l_SrcVec4.x), ceil(l_SrcVec4.y), ceil(l_SrcVec4.z), ceil(l_SrcVec4.w));
		MDOperandValue* l_pOperand = DOME_New(MDOperandValue)(RCGlobal::k_SimpleTypeID_DVector4f);
		l_pOperand->getDataPtr()->setDVector4f(l_ResultVec4);
		FRAMETIMER_END(FTT_RC_CAL_FLOATCEIL);
        return l_pOperand;
	}
	FRAMETIMER_END(FTT_RC_CAL_FLOATCEIL);
	return NULL;
}

DResult             MDOperatorFloatCeil::destroyResult(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, MDOperandPtr i_pResult) const
{
	DOME_Del(i_pResult);
	return R_SUCCESS;
}


RC_NAMESPACE_END