#include "pch.h"
/*
    filename:       MDOperatorConvert2F1.cpp
    author:         Ming Dong
    date:           2016-JUN-28
    description:    
*/

#include "MDOperatorConvert2F1.h"
#include <rc/public/iexecuter.h>

#ifdef RC_PERF
#include "KG3D_FrameTimer.h"
#else
#define FRAMETIMER_BEGIN(a,b)
#define FRAMETIMER_END(a)
#endif


RC_NAMESPACE_BEGIN

MDOperatorConvert2F1::MDOperatorConvert2F1()
    : m_OperatorName("MDConvert2F1")
{

}

MDOperatorConvert2F1::~MDOperatorConvert2F1()
{

}

/****************************
    FROM MDOperator class
****************************/
const DString&      MDOperatorConvert2F1::getOperatorName() const
{
    return m_OperatorName;
}

Bool                MDOperatorConvert2F1::isGpuOperator() const
{
    return DM_FALSE;
}

Int                 MDOperatorConvert2F1::getInputCount() const
{
    return 1;
}

DSimpleTypeID       MDOperatorConvert2F1::getInputTypeID(Int i_Index) const
{
    return RCGlobal::k_SimpleTypeID_Unknown;
}

Int                 MDOperatorConvert2F1::getOutputCount() const
{
    return 1;
}

DSimpleTypeID       MDOperatorConvert2F1::getOutputTypeID(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_Index == 0);
    DOME_ASSERT(i_ParamCount == 1);
    DOME_ASSERT(i_pParamList[0]->getDataCount() == 1);
    return RCGlobal::k_SimpleTypeID_F32;
}

RCGPUDATAFORMAT     MDOperatorConvert2F1::getOutputTexFmt(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_Index == 0);
    DOME_ASSERT(i_ParamCount == 1);
	return RGDF_UNKNOWN;
}

DResult             MDOperatorConvert2F1::calcOutputTexSize(Int i_Index, MDEffect* i_pMDEffect, DVector2i& o_Size, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_Index == 0);
    DOME_ASSERT(i_ParamCount == 1);

    return FALSE;
}

/****************************
    FROM MDOperatorCpu class
****************************/
MDOperandPtr        MDOperatorConvert2F1::execute(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, IRP* o_pInputReleasePoint) const
{
    PERF_COUNTER_EX(0);
	FRAMETIMER_BEGIN(FTT_RC_CAL_C2F1, FTT_RC_CAL_EXECUTE_EFFECTPASS);
    DOME_ASSERT(i_ParamCount == 1);
    DOME_ASSERT(i_pParamList[0]);      // source value
	DOME_ASSERT(i_pParamList[0]->getDataCount() == 1);
      
	DSimpleTypeID l_TypeID = i_pParamList[0]->getDataType(0);

	if (l_TypeID == RCGlobal::k_SimpleTypeID_F32)									//-无需转换
	{
		o_pInputReleasePoint[0] = IRP_INFINISHCALLBACK;
        FRAMETIMER_END(FTT_RC_CAL_C2F1);
		return i_pParamList[0];
	}
	else if (l_TypeID == RCGlobal::k_SimpleTypeID_DVector2f)							//-DVector2f --- F32
	{
		o_pInputReleasePoint[0] = IRP_AFTEREXECUTE;

		DSimpleTypedValue* l_pSrc = i_pParamList[0]->getDataPtr();   
		DOME_ASSERT(l_pSrc);

		DVector2f l_SrcVec2 = l_pSrc->getDVector2f();
		F32 l_ResultF32 = l_SrcVec2.x;
		MDOperandValue* l_pOperand = DOME_New(MDOperandValue)(RCGlobal::k_SimpleTypeID_F32);
		l_pOperand->getDataPtr()->setF32(l_ResultF32);
        FRAMETIMER_END(FTT_RC_CAL_C2F1);
		return l_pOperand;
	}
	else if (l_TypeID == RCGlobal::k_SimpleTypeID_DVector3f)							//-DVector3f --- F32
	{
		o_pInputReleasePoint[0] = IRP_AFTEREXECUTE;

		DSimpleTypedValue* l_pSrc = i_pParamList[0]->getDataPtr();   
		DOME_ASSERT(l_pSrc);

		DVector3f l_SrcVec3 = l_pSrc->getDVector3f();
		F32 l_ResultF32 = l_SrcVec3.x;
		MDOperandValue* l_pOperand = DOME_New(MDOperandValue)(RCGlobal::k_SimpleTypeID_F32);
		l_pOperand->getDataPtr()->setF32(l_ResultF32);
        FRAMETIMER_END(FTT_RC_CAL_C2F1);
		return l_pOperand;
	}
	else if (l_TypeID == RCGlobal::k_SimpleTypeID_DVector4f)							//-DVector4f --- F32
	{
		o_pInputReleasePoint[0] = IRP_AFTEREXECUTE;

		DSimpleTypedValue* l_pSrc = i_pParamList[0]->getDataPtr();   
		DOME_ASSERT(l_pSrc);

		DVector4f l_SrcVec4 = l_pSrc->getDVector4f();
		F32 l_ResultF32 = l_SrcVec4.x;
		MDOperandValue* l_pOperand = DOME_New(MDOperandValue)(RCGlobal::k_SimpleTypeID_F32);
		l_pOperand->getDataPtr()->setF32(l_ResultF32);
        FRAMETIMER_END(FTT_RC_CAL_C2F1);
		return l_pOperand;
	}
	FRAMETIMER_END(FTT_RC_CAL_C2F1);
	return NULL;
}

DResult             MDOperatorConvert2F1::destroyResult(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, MDOperandPtr i_pResult) const
{
	DOME_ASSERT(i_ParamCount == 1);
	DOME_ASSERT(i_pParamList[0]);      // source value
	DOME_ASSERT(i_pParamList[0]->getDataCount() == 1);

	DSimpleTypeID l_TypeID = i_pParamList[0]->getDataType(0);

	if (l_TypeID == RCGlobal::k_SimpleTypeID_F32)									//-无需转换
	{
		return R_SUCCESS;
	}
	else if (l_TypeID == RCGlobal::k_SimpleTypeID_DVector2f || l_TypeID == RCGlobal::k_SimpleTypeID_DVector3f || l_TypeID == RCGlobal::k_SimpleTypeID_DVector4f)							//-DVector2f --- F32
	{
		DOME_ASSERT(i_pResult);
		DOME_Del(i_pResult);
		return R_SUCCESS;
	}

	return R_FAILED;

}


RC_NAMESPACE_END