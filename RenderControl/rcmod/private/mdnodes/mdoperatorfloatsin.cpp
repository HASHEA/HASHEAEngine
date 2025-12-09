#include "pch.h"
/*
    filename:       mdoperatorcopyvalueto.cpp
    author:         Ming Dong
    date:           2016-JUN-28
    description:    
*/

#include "mdoperatorfloatsin.h"
#include <rc/public/iexecuter.h>

#ifdef RC_PERF
#include "KG3D_FrameTimer.h"
#else
#define FRAMETIMER_BEGIN(a,b)
#define FRAMETIMER_END(a)
#endif


RC_NAMESPACE_BEGIN

MDOperatorFloatSin::MDOperatorFloatSin()
    : m_OperatorName("MDFloatSin")
{

}

MDOperatorFloatSin::~MDOperatorFloatSin()
{

}

/****************************
    FROM MDOperator class
****************************/
const DString&      MDOperatorFloatSin::getOperatorName() const
{
    return m_OperatorName;
}

Bool                MDOperatorFloatSin::isGpuOperator() const
{
    return DM_FALSE;
}

Int                 MDOperatorFloatSin::getInputCount() const
{
    return 1;
}

DSimpleTypeID       MDOperatorFloatSin::getInputTypeID(Int i_Index) const
{
    return RCGlobal::k_SimpleTypeID_Unknown;
}

Int                 MDOperatorFloatSin::getOutputCount() const
{
    return 1;
}

DSimpleTypeID       MDOperatorFloatSin::getOutputTypeID(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_Index == 0);
    DOME_ASSERT(i_ParamCount == 1);
    DOME_ASSERT(i_pParamList[0]->getDataCount() == 1);
    return i_pParamList[0]->getDataType(0);
}

RCGPUDATAFORMAT     MDOperatorFloatSin::getOutputTexFmt(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_Index == 0);
    DOME_ASSERT(i_ParamCount == 1);
    
    return RGDF_UNKNOWN;
}

DResult             MDOperatorFloatSin::calcOutputTexSize(Int i_Index, MDEffect* i_pMDEffect, DVector2i& o_Size, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    return i_pParamList[0]->getTextureSize(o_Size);
}

/****************************
    FROM MDOperatorCpu class
****************************/
MDOperandPtr        MDOperatorFloatSin::execute(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, IRP* o_pInputReleasePoint) const
{
	FRAMETIMER_BEGIN(FTT_RC_CAL_FLOATSIN, FTT_RC_CAL_EXECUTE_EFFECTPASS);
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
		F32 l_ResultF32 = sin(l_fSrc);
		MDOperandValue* l_pOperand = DOME_New(MDOperandValue)(RCGlobal::k_SimpleTypeID_F32);
		l_pOperand->getDataPtr()->setF32(l_ResultF32);
        FRAMETIMER_END(FTT_RC_CAL_FLOATSIN);
		return l_pOperand;
	}
	else if (l_TypeID == RCGlobal::k_SimpleTypeID_DVector2f)						
	{
		DVector2f l_SrcVec2 = l_pSrc->getDVector2f();
		DVector2f l_ResultVec2 = DVector2f(sin(l_SrcVec2.x), sin(l_SrcVec2.y));
		MDOperandValue* l_pOperand = DOME_New(MDOperandValue)(RCGlobal::k_SimpleTypeID_DVector2f);
		l_pOperand->getDataPtr()->setDVector2f(l_ResultVec2);
        FRAMETIMER_END(FTT_RC_CAL_FLOATSIN);
		return l_pOperand;
	}
	else if (l_TypeID == RCGlobal::k_SimpleTypeID_DVector3f)						
	{
		DVector3f l_SrcVec3 = l_pSrc->getDVector3f();
		DVector3f l_ResultVec3 = DVector3f(sin(l_SrcVec3.x), sin(l_SrcVec3.y), sin(l_SrcVec3.z));
		MDOperandValue* l_pOperand = DOME_New(MDOperandValue)(RCGlobal::k_SimpleTypeID_DVector3f);
		l_pOperand->getDataPtr()->setDVector3f(l_ResultVec3);
        FRAMETIMER_END(FTT_RC_CAL_FLOATSIN);
		return l_pOperand;
	}
	else if (l_TypeID == RCGlobal::k_SimpleTypeID_DVector4f)						
	{
		DVector4f l_SrcVec4 = l_pSrc->getDVector4f();
		DVector4f l_ResultVec4 = DVector4f(sin(l_SrcVec4.x), sin(l_SrcVec4.y), sin(l_SrcVec4.z), sin(l_SrcVec4.w));
		MDOperandValue* l_pOperand = DOME_New(MDOperandValue)(RCGlobal::k_SimpleTypeID_DVector4f);
		l_pOperand->getDataPtr()->setDVector4f(l_ResultVec4);
        FRAMETIMER_END(FTT_RC_CAL_FLOATSIN);
		return l_pOperand;
	}

	FRAMETIMER_END(FTT_RC_CAL_FLOATSIN);

	return NULL;
}

DResult             MDOperatorFloatSin::destroyResult(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, MDOperandPtr i_pResult) const
{
	DOME_Del(i_pResult);
	return R_SUCCESS;
}


RC_NAMESPACE_END