#include "pch.h"
#include "mdoperatorexp2f32.h"
#include <rc/public/iexecuter.h>

#ifdef RC_PERF
#include "KG3D_FrameTimer.h"
#else
#define FRAMETIMER_BEGIN(a,b)
#define FRAMETIMER_END(a)
#endif


RC_NAMESPACE_BEGIN

MDOperatorExp2F32::MDOperatorExp2F32()
    : m_OperatorName("MDPowF32")
{

}

MDOperatorExp2F32::~MDOperatorExp2F32()
{

}

/****************************
    FROM MDOperator class
****************************/
const DString&      MDOperatorExp2F32::getOperatorName() const
{
    return m_OperatorName;
}

Bool                MDOperatorExp2F32::isGpuOperator() const
{
    return DM_FALSE;
}

Int                 MDOperatorExp2F32::getInputCount() const
{
    return 1;
}

DSimpleTypeID       MDOperatorExp2F32::getInputTypeID(Int i_Index) const
{
    return RCGlobal::k_SimpleTypeID_F32;
}

Int                 MDOperatorExp2F32::getOutputCount() const
{
    return 1;
}

DSimpleTypeID       MDOperatorExp2F32::getOutputTypeID(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_Index == 0);
    DOME_ASSERT(i_ParamCount == 1);
    DOME_ASSERT(i_pParamList[0]->getDataCount() == 1);
    return RCGlobal::k_SimpleTypeID_F32;
}

RCGPUDATAFORMAT     MDOperatorExp2F32::getOutputTexFmt(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_Index == 0);
    DOME_ASSERT(i_ParamCount == 1);
    
    return RGDF_UNKNOWN;
}

DResult             MDOperatorExp2F32::calcOutputTexSize(Int i_Index, MDEffect* i_pMDEffect, DVector2i& o_Size, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    return FALSE;
}

/****************************
    FROM MDOperatorCpu class
****************************/
MDOperandPtr        MDOperatorExp2F32::execute(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, IRP* o_pInputReleasePoint) const
{
    PERF_COUNTER_EX(0);
	FRAMETIMER_BEGIN(FTT_RC_CAL_FLOATFRAC, FTT_RC_CAL_EXECUTE_EFFECTPASS);
	DOME_ASSERT(i_ParamCount == 1);
	DOME_ASSERT(i_pParamList[0]);      // source value
	DOME_ASSERT(i_pParamList[0]->getDataCount() == 1);

    o_pInputReleasePoint[0] = IRP_AFTEREXECUTE;

	DSimpleTypeID l_TypeID = i_pParamList[0]->getDataType(0);
	DSimpleTypedValue* l_pSrc = i_pParamList[0]->getDataPtr();   
	DOME_ASSERT(l_pSrc);

	DOME_ASSERT (l_TypeID == RCGlobal::k_SimpleTypeID_F32);

	{
		F32 l_fSrc = l_pSrc->getF32();
		F32 l_ResultF32 = ldexp(1, l_fSrc);
		MDOperandValue* l_pOperand = DOME_New(MDOperandValue)(RCGlobal::k_SimpleTypeID_F32);
		l_pOperand->getDataPtr()->setF32(l_ResultF32);
        FRAMETIMER_END(FTT_RC_CAL_FLOATFRAC);
		return l_pOperand;
	}

	FRAMETIMER_END(FTT_RC_CAL_FLOATFRAC);
	return NULL;
}

DResult             MDOperatorExp2F32::destroyResult(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, MDOperandPtr i_pResult) const
{
	DOME_Del(i_pResult);
	return R_SUCCESS;
}


RC_NAMESPACE_END