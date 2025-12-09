#include "pch.h"
/*
    filename:       MDOperatorConvert2F2.cpp
    author:         Ming Dong
    date:           2016-JUN-28
    description:    
*/

#include "MDOperatorConvert2F2.h"
#include <rc/public/iexecuter.h>

#ifdef RC_PERF
#include "KG3D_FrameTimer.h"
#else
#define FRAMETIMER_BEGIN(a,b)
#define FRAMETIMER_END(a)
#endif


RC_NAMESPACE_BEGIN

MDOperatorConvert2F2::MDOperatorConvert2F2()
    : m_OperatorName("MDConvert2F2")
{

}

MDOperatorConvert2F2::~MDOperatorConvert2F2()
{

}

/****************************
    FROM MDOperator class
****************************/
const DString&      MDOperatorConvert2F2::getOperatorName() const
{
    return m_OperatorName;
}

Bool                MDOperatorConvert2F2::isGpuOperator() const
{
    return DM_FALSE;
}

Int                 MDOperatorConvert2F2::getInputCount() const
{
    return 1;
}

DSimpleTypeID       MDOperatorConvert2F2::getInputTypeID(Int i_Index) const
{
    return RCGlobal::k_SimpleTypeID_Unknown;
}

Int                 MDOperatorConvert2F2::getOutputCount() const
{
    return 1;
}

DSimpleTypeID       MDOperatorConvert2F2::getOutputTypeID(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_Index == 0);
    DOME_ASSERT(i_ParamCount == 1);
    DOME_ASSERT(i_pParamList[0]->getDataCount() == 1);
    return RCGlobal::k_SimpleTypeID_DVector2f; /*i_pParamList[0]->getDataType(0);*/
}

RCGPUDATAFORMAT     MDOperatorConvert2F2::getOutputTexFmt(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_Index == 0);
    DOME_ASSERT(i_ParamCount == 1);
	return RGDF_UNKNOWN;
}

DResult             MDOperatorConvert2F2::calcOutputTexSize(Int i_Index, MDEffect* i_pMDEffect, DVector2i& o_Size, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_Index == 0);
    DOME_ASSERT(i_ParamCount == 1);

    return FALSE;
}

/****************************
    FROM MDOperatorCpu class
****************************/
MDOperandPtr        MDOperatorConvert2F2::execute(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, IRP* o_pInputReleasePoint) const
{
    PERF_COUNTER_EX(0);
	FRAMETIMER_BEGIN(FTT_RC_CAL_C2F2, FTT_RC_CAL_EXECUTE_EFFECTPASS);
    DOME_ASSERT(i_ParamCount == 1);
    DOME_ASSERT(i_pParamList[0]);      // source value
	DOME_ASSERT(i_pParamList[0]->getDataCount() == 1);
      
	DSimpleTypeID l_TypeID = i_pParamList[0]->getDataType(0);

	if (l_TypeID == RCGlobal::k_SimpleTypeID_F32)									//-F32 --- DVector2F
	{
		o_pInputReleasePoint[0] = IRP_AFTEREXECUTE;

		DSimpleTypedValue* l_pSrc = i_pParamList[0]->getDataPtr();   
		DOME_ASSERT(l_pSrc);

		F32 l_SrcF32 = l_pSrc->getF32();

		DVector2f l_ResultVec2;
		l_ResultVec2.x = l_SrcF32;
		l_ResultVec2.y = l_SrcF32;
		
		MDOperandValue* l_pOperand = DOME_New(MDOperandValue)(RCGlobal::k_SimpleTypeID_DVector2f);
		l_pOperand->getDataPtr()->setDVector2f(l_ResultVec2);
        FRAMETIMER_END(FTT_RC_CAL_C2F2);
		return l_pOperand;
	}
	else if (l_TypeID == RCGlobal::k_SimpleTypeID_DVector2f)							//-无需转换
	{
		o_pInputReleasePoint[0] = IRP_INFINISHCALLBACK;
        FRAMETIMER_END(FTT_RC_CAL_C2F2);
		return i_pParamList[0];
	}
	else if (l_TypeID == RCGlobal::k_SimpleTypeID_DVector3f)							//-DVector3f --- DVector2F
	{
		o_pInputReleasePoint[0] = IRP_AFTEREXECUTE;
		
		DSimpleTypedValue* l_pSrc = i_pParamList[0]->getDataPtr();   
		DOME_ASSERT(l_pSrc);

		DVector3f l_SrcVec3 = l_pSrc->getDVector3f();

		DVector2f l_ResultVec2;
		l_ResultVec2.x = l_SrcVec3.x;
		l_ResultVec2.y = l_SrcVec3.y;

		MDOperandValue* l_pOperand = DOME_New(MDOperandValue)(RCGlobal::k_SimpleTypeID_DVector2f);
		l_pOperand->getDataPtr()->setDVector2f(l_ResultVec2);
        FRAMETIMER_END(FTT_RC_CAL_C2F2);
		return l_pOperand;
	}
	else if (l_TypeID == RCGlobal::k_SimpleTypeID_DVector4f)							//-DVector4f --- DVector2F
	{
		o_pInputReleasePoint[0] = IRP_AFTEREXECUTE;

		DSimpleTypedValue* l_pSrc = i_pParamList[0]->getDataPtr();   
		DOME_ASSERT(l_pSrc);

		DVector4f l_SrcVec4 = l_pSrc->getDVector4f();

		DVector2f l_ResultVec2;
		l_ResultVec2.x = l_SrcVec4.x;
		l_ResultVec2.y = l_SrcVec4.y;

		MDOperandValue* l_pOperand = DOME_New(MDOperandValue)(RCGlobal::k_SimpleTypeID_DVector2f);
		l_pOperand->getDataPtr()->setDVector2f(l_ResultVec2);
        FRAMETIMER_END(FTT_RC_CAL_C2F2);
		return l_pOperand;
	}
	FRAMETIMER_END(FTT_RC_CAL_C2F2);
	return NULL;
}

DResult             MDOperatorConvert2F2::destroyResult(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, MDOperandPtr i_pResult) const
{
	DOME_ASSERT(i_ParamCount == 1);
	DOME_ASSERT(i_pParamList[0]);      // source value
	DOME_ASSERT(i_pParamList[0]->getDataCount() == 1);

	DSimpleTypeID l_TypeID = i_pParamList[0]->getDataType(0);

	if (l_TypeID == RCGlobal::k_SimpleTypeID_DVector2f)									//-无需转换
	{
		return R_SUCCESS;
	}
	else if (l_TypeID == RCGlobal::k_SimpleTypeID_F32 || l_TypeID == RCGlobal::k_SimpleTypeID_DVector3f || l_TypeID == RCGlobal::k_SimpleTypeID_DVector4f)							//-DVector2f --- F32
	{
		DOME_ASSERT(i_pResult);
		DOME_Del(i_pResult);
		return R_SUCCESS;
	}

	return R_FAILED;
}

RC_NAMESPACE_END