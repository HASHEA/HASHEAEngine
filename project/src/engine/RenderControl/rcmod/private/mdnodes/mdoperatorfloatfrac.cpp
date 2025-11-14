#include "pch.h"
/*
    filename:       mdoperatorcopyvalueto.cpp
    author:         Ming Dong
    date:           2016-JUN-28
    description:    
*/

#include "mdoperatorfloatfrac.h"
#include <rc/public/iexecuter.h>

#ifdef RC_PERF
#include "KG3D_FrameTimer.h"
#else
#define FRAMETIMER_BEGIN(a,b)
#define FRAMETIMER_END(a)
#endif


RC_NAMESPACE_BEGIN

MDOperatorFloatFrac::MDOperatorFloatFrac()
    : m_OperatorName("MDFloatFrac")
{

}

MDOperatorFloatFrac::~MDOperatorFloatFrac()
{

}

/****************************
    FROM MDOperator class
****************************/
const DString&      MDOperatorFloatFrac::getOperatorName() const
{
    return m_OperatorName;
}

Bool                MDOperatorFloatFrac::isGpuOperator() const
{
    return DM_FALSE;
}

Int                 MDOperatorFloatFrac::getInputCount() const
{
    return 1;
}

DSimpleTypeID       MDOperatorFloatFrac::getInputTypeID(Int i_Index) const
{
    return RCGlobal::k_SimpleTypeID_Unknown;
}

Int                 MDOperatorFloatFrac::getOutputCount() const
{
    return 1;
}

DSimpleTypeID       MDOperatorFloatFrac::getOutputTypeID(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_Index == 0);
    DOME_ASSERT(i_ParamCount == 1);
    DOME_ASSERT(i_pParamList[0]->getDataCount() == 1);
    return i_pParamList[0]->getDataType(0);
}

RCGPUDATAFORMAT     MDOperatorFloatFrac::getOutputTexFmt(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_Index == 0);
    DOME_ASSERT(i_ParamCount == 1);
    
    return RGDF_UNKNOWN;
}

DResult             MDOperatorFloatFrac::calcOutputTexSize(Int i_Index, MDEffect* i_pMDEffect, DVector2i& o_Size, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    return i_pParamList[0]->getTextureSize(o_Size);
}

/****************************
    FROM MDOperatorCpu class
****************************/
MDOperandPtr        MDOperatorFloatFrac::execute(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, IRP* o_pInputReleasePoint) const
{
	FRAMETIMER_BEGIN(FTT_RC_CAL_FLOATFRAC, FTT_RC_CAL_EXECUTE_EFFECTPASS);
	DOME_ASSERT(i_ParamCount == 1);
	DOME_ASSERT(i_pParamList[0]);      // source value
	DOME_ASSERT(i_pParamList[0]->getDataCount() == 1);

    o_pInputReleasePoint[0] = IRP_AFTEREXECUTE;

	DSimpleTypeID l_TypeID = i_pParamList[0]->getDataType(0);
	DSimpleTypedValue* l_pSrc = i_pParamList[0]->getDataPtr();   
	DOME_ASSERT(l_pSrc);

	float IntegerPart;
	if (l_TypeID == RCGlobal::k_SimpleTypeID_F32)									
	{
		F32 l_fSrc = l_pSrc->getF32();
		F32 l_ResultF32 = modf(l_fSrc, &IntegerPart);
		MDOperandValue* l_pOperand = DOME_New(MDOperandValue)(RCGlobal::k_SimpleTypeID_F32);
		l_pOperand->getDataPtr()->setF32(l_ResultF32);
        FRAMETIMER_END(FTT_RC_CAL_FLOATFRAC);
		return l_pOperand;
	}
	else if (l_TypeID == RCGlobal::k_SimpleTypeID_DVector2f)						
	{
		DVector2f l_SrcVec2 = l_pSrc->getDVector2f();
		DVector2f l_ResultVec2 = DVector2f(modf(l_SrcVec2.x, &IntegerPart), modf(l_SrcVec2.y, &IntegerPart));
		MDOperandValue* l_pOperand = DOME_New(MDOperandValue)(RCGlobal::k_SimpleTypeID_DVector2f);
		l_pOperand->getDataPtr()->setDVector2f(l_ResultVec2);
        FRAMETIMER_END(FTT_RC_CAL_FLOATFRAC);
		return l_pOperand;
	}
	else if (l_TypeID == RCGlobal::k_SimpleTypeID_DVector3f)						
	{
		DVector3f l_SrcVec3 = l_pSrc->getDVector3f();
		DVector3f l_ResultVec3 = DVector3f(modf(l_SrcVec3.x, &IntegerPart), modf(l_SrcVec3.y, &IntegerPart), modf(l_SrcVec3.z, &IntegerPart));
		MDOperandValue* l_pOperand = DOME_New(MDOperandValue)(RCGlobal::k_SimpleTypeID_DVector3f);
		l_pOperand->getDataPtr()->setDVector3f(l_ResultVec3);
        FRAMETIMER_END(FTT_RC_CAL_FLOATFRAC);
		return l_pOperand;
	}
	else if (l_TypeID == RCGlobal::k_SimpleTypeID_DVector4f)						
	{
		DVector4f l_SrcVec4 = l_pSrc->getDVector4f();
		DVector4f l_ResultVec4 = DVector4f(modf(l_SrcVec4.x, &IntegerPart), modf(l_SrcVec4.y, &IntegerPart), modf(l_SrcVec4.z, &IntegerPart), modf(l_SrcVec4.w, &IntegerPart));
		MDOperandValue* l_pOperand = DOME_New(MDOperandValue)(RCGlobal::k_SimpleTypeID_DVector4f);
		l_pOperand->getDataPtr()->setDVector4f(l_ResultVec4);
        FRAMETIMER_END(FTT_RC_CAL_FLOATFRAC);
		return l_pOperand;
	}
	FRAMETIMER_END(FTT_RC_CAL_FLOATFRAC);
	return NULL;
}

DResult             MDOperatorFloatFrac::destroyResult(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, MDOperandPtr i_pResult) const
{
	DOME_Del(i_pResult);
	return R_SUCCESS;
}


RC_NAMESPACE_END