#include "pch.h"
/*
filename:       mdoperatorconfigurewaterparam.cpp
author:         Bin Yang
date:           2018-Nov-20
description:
*/
#include "mdoperatorconfigurewaterparam.h"
#include <rc/public/iexecuter.h>

RC_NAMESPACE_BEGIN

static unsigned int nParamCount = 4 + 3;

MDOperatorConfigureWaterParam::MDOperatorConfigureWaterParam()
	: m_OperatorName("MDConfigureWaterParam")
{
}

MDOperatorConfigureWaterParam::~MDOperatorConfigureWaterParam()
{
}

/****************************
FROM MDOperator class
****************************/
const DString& MDOperatorConfigureWaterParam::getOperatorName() const
{
	return m_OperatorName;
}

Bool MDOperatorConfigureWaterParam::isGpuOperator() const
{
	return DM_FALSE;
}

Int MDOperatorConfigureWaterParam::getInputCount() const
{
	return nParamCount;
}

DSimpleTypeID MDOperatorConfigureWaterParam::getInputTypeID(Int i_Index) const
{
	DSimpleTypeID l_ParamTypeID;

	switch (i_Index)
	{
	case 0:
	case 1:
	case 2:
		l_ParamTypeID = RCGlobal::k_SimpleTypeID_DMatrix3x3f;
		break;
	case 3:
	case 4:
	case 5:
	case 6:
		l_ParamTypeID = RCGlobal::k_SimpleTypeID_F32;
		break;
	}

	return l_ParamTypeID;
}

Int MDOperatorConfigureWaterParam::getOutputCount() const
{
	return 3;
}

DSimpleTypeID MDOperatorConfigureWaterParam::getOutputTypeID(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
	DOME_ASSERT(i_ParamCount == nParamCount);
	return RCGlobal::k_SimpleTypeID_DMatrix3x3f;
}

RCGPUDATAFORMAT MDOperatorConfigureWaterParam::getOutputTexFmt(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
	DOME_ASSERT(i_ParamCount == nParamCount);
	DOME_ASSERT(i_pParamList[0] && i_pParamList[0]->isMatrix3x3());
	DOME_ASSERT(i_pParamList[1] && i_pParamList[1]->isMatrix3x3());
	DOME_ASSERT(i_pParamList[2] && i_pParamList[2]->isMatrix3x3());
	DOME_ASSERT(i_pParamList[3] && i_pParamList[3]->isFloat());
	DOME_ASSERT(i_pParamList[4] && i_pParamList[4]->isFloat());
	DOME_ASSERT(i_pParamList[5] && i_pParamList[5]->isFloat());
	DOME_ASSERT(i_pParamList[6] && i_pParamList[6]->isFloat());

	return RGDF_UNKNOWN;
}

DResult MDOperatorConfigureWaterParam::calcOutputTexSize(Int i_Index, MDEffect* i_pMDEffect, DVector2i& o_Size, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
	DOME_ASSERT(i_ParamCount == nParamCount);
	DOME_ASSERT(i_pParamList[0] && i_pParamList[0]->isMatrix3x3());
	DOME_ASSERT(i_pParamList[1] && i_pParamList[1]->isMatrix3x3());
	DOME_ASSERT(i_pParamList[2] && i_pParamList[2]->isMatrix3x3());
	DOME_ASSERT(i_pParamList[3] && i_pParamList[3]->isFloat());
	DOME_ASSERT(i_pParamList[4] && i_pParamList[4]->isFloat());
	DOME_ASSERT(i_pParamList[5] && i_pParamList[5]->isFloat());
	DOME_ASSERT(i_pParamList[6] && i_pParamList[6]->isFloat());

	return R_FAILED;
}

/****************************
FROM MDOperatorCpu class
****************************/
MDOperandPtr MDOperatorConfigureWaterParam::execute(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, IRP* o_pInputReleasePoint) const
{
    PERF_COUNTER_EX(0);
	DOME_ASSERT(i_ParamCount == nParamCount);
	DOME_ASSERT(i_pParamList[0] && i_pParamList[0]->isMatrix3x3());
	DOME_ASSERT(i_pParamList[1] && i_pParamList[1]->isMatrix3x3());
	DOME_ASSERT(i_pParamList[2] && i_pParamList[2]->isMatrix3x3());
	DOME_ASSERT(i_pParamList[3] && i_pParamList[3]->isFloat());
	DOME_ASSERT(i_pParamList[4] && i_pParamList[4]->isFloat());
	DOME_ASSERT(i_pParamList[5] && i_pParamList[5]->isFloat());
	DOME_ASSERT(i_pParamList[6] && i_pParamList[6]->isFloat());

	o_pInputReleasePoint[0] = IRP_INFINISHCALLBACK;
	o_pInputReleasePoint[1] = IRP_INFINISHCALLBACK;
	o_pInputReleasePoint[2] = IRP_INFINISHCALLBACK;
	o_pInputReleasePoint[3] = IRP_INFINISHCALLBACK;
	o_pInputReleasePoint[4] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[5] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[6] = IRP_AFTEREXECUTE;

	DResult dResult;
	DSimpleTypedValue* l_pMatAvgHeights = i_pParamList[0]->getDataPtr();
	DSimpleTypedValue* l_pMatThresholdAbove = i_pParamList[1]->getDataPtr();
	DSimpleTypedValue* l_pMatThresholdUnder = i_pParamList[2]->getDataPtr();
	DMatrix3x3f mat3x3AvgHeights, mat3x3ThresholdAbove, mat3x3ThresholdUnder;
	mat3x3AvgHeights = l_pMatAvgHeights->getDMatrix3x3f();
	mat3x3ThresholdAbove = l_pMatThresholdAbove->getDMatrix3x3f();
	mat3x3ThresholdUnder = l_pMatThresholdUnder->getDMatrix3x3f();
	F32 fOceanIndex, fLowestHeightOffset, fAbsoluteThresholdAbove, fAbsoluteThresholdUnder;
	dResult = i_pParamList[3]->getFloat(fOceanIndex);
	DOME_ASSERT(DM_SUCC(dResult));
	dResult = i_pParamList[4]->getFloat(fLowestHeightOffset);
	DOME_ASSERT(DM_SUCC(dResult));
	dResult = i_pParamList[5]->getFloat(fAbsoluteThresholdAbove);
	DOME_ASSERT(DM_SUCC(dResult));
	dResult = i_pParamList[6]->getFloat(fAbsoluteThresholdUnder);
	DOME_ASSERT(DM_SUCC(dResult));

	MDOperandValue* l_pOperandAvgHeights = DOME_New(MDOperandValue)(RCGlobal::k_SimpleTypeID_DMatrix3x3f);
	MDOperandValue* l_pOperandThresholdAbove = DOME_New(MDOperandValue)(RCGlobal::k_SimpleTypeID_DMatrix3x3f);
	MDOperandValue* l_pOperandThresholdUnder = DOME_New(MDOperandValue)(RCGlobal::k_SimpleTypeID_DMatrix3x3f);
	//if (fOceanIndex >= 0)
	//{
	//	Int nOceanIndex = (Int)fOceanIndex;
	//	DOME_ASSERT(fOceanIndex < 9);
	//	mat3x3AvgHeights.m[nOceanIndex] += fLowestHeightOffset;
	//	if (fAbsoluteThresholdAbove >= 0.0f)
	//	{
	//		mat3x3ThresholdAbove.m[nOceanIndex] = fAbsoluteThresholdAbove;
	//	}
	//	if (fAbsoluteThresholdUnder >= 0.0f)
	//	{
	//		mat3x3ThresholdUnder.m[nOceanIndex] = fAbsoluteThresholdUnder;
	//	}
	//}
	dResult = l_pOperandAvgHeights->getDataPtr()->setDMatrix3x3f(mat3x3AvgHeights);
	DOME_ASSERT(DM_SUCC(dResult));
	dResult = l_pOperandThresholdAbove->getDataPtr()->setDMatrix3x3f(mat3x3ThresholdAbove);
	DOME_ASSERT(DM_SUCC(dResult));
	dResult = l_pOperandThresholdUnder->getDataPtr()->setDMatrix3x3f(mat3x3ThresholdUnder);
	DOME_ASSERT(DM_SUCC(dResult));

	MDOperandArray* l_pOperand = DOME_New(MDOperandArray);
	l_pOperand->addOperand(l_pOperandAvgHeights);
	l_pOperand->addOperand(l_pOperandThresholdAbove);
	l_pOperand->addOperand(l_pOperandThresholdUnder);

	return l_pOperand;
}

DResult MDOperatorConfigureWaterParam::destroyResult(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, MDOperandPtr i_pResult) const
{
	for (int i = 0; i < 3; ++i)
	{
		MDOperandValue* l_pSubOperand = (MDOperandValue*)i_pResult->getSubOperand(i);
		DOME_Del(l_pSubOperand);
	}

	DOME_Del(i_pResult);

	return R_SUCCESS;
}

RC_NAMESPACE_END