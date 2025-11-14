#include "pch.h"
/*
    filename:       mdoperatorarrayselector.cpp
    author:         Ming Dong
    date:           2016-SEP-11
    description:    
*/

#include "mdoperatorarrayselector.h"

#ifdef RC_PERF
#include "KG3D_FrameTimer.h"
#else
#define FRAMETIMER_BEGIN(a,b)
#define FRAMETIMER_END(a)
#endif


RC_NAMESPACE_BEGIN

MDOperatorArraySelector::MDOperatorArraySelector()
    : m_OperatorName("MDArraySelector")
{

}

MDOperatorArraySelector::~MDOperatorArraySelector()
{

}

/****************************
    FROM MDOperator class
****************************/
const DString&      MDOperatorArraySelector::getOperatorName() const
{
    return m_OperatorName;
}

Bool                MDOperatorArraySelector::isGpuOperator() const
{
    return DM_FALSE;
}

Int                 MDOperatorArraySelector::getInputCount() const
{
    return 2;
}

DSimpleTypeID       MDOperatorArraySelector::getInputTypeID(Int i_Index) const
{
	if (i_Index == 0)
		return RCGlobal::k_SimpleTypeID_Unknown;
	else
		return RCGlobal::k_SimpleTypeID_Int;
}

Int                 MDOperatorArraySelector::getOutputCount() const
{
    return 1;
}

DSimpleTypeID       MDOperatorArraySelector::getOutputTypeID(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_ParamCount == 2);
	DOME_ASSERT(i_pParamList[1]->getDataType(0) == RCGlobal::k_SimpleTypeID_Int);
	DOME_ASSERT(i_pParamList[1]->getDataPtr());

	Int l_Index = i_pParamList[1]->getDataPtr()->getInt();
    return i_pParamList[0]->getDataType(l_Index);
}

RCGPUDATAFORMAT     MDOperatorArraySelector::getOutputTexFmt(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_ParamCount == 2);
    if (getOutputTypeID(i_Index, i_pMDEffect, i_ParamCount, i_pParamList) == RCGlobal::k_SimpleTypeID_OSTexture2D)
    {
		DOME_ASSERT(i_ParamCount == 2);
		DOME_ASSERT(i_pParamList[1]->getDataType(0) == RCGlobal::k_SimpleTypeID_Int);
		DOME_ASSERT(i_pParamList[1]->getDataPtr());

		Int l_Index = i_pParamList[1]->getDataPtr()->getInt();

        return i_pParamList[0]->getTexDataFmt(l_Index);
    }
    else
        return RGDF_UNKNOWN;
}

DResult             MDOperatorArraySelector::calcOutputTexSize(Int i_Index, MDEffect* i_pMDEffect, DVector2i& o_Size, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_ParamCount == 2);
	DOME_ASSERT(i_pParamList[1]->getDataType(0) == RCGlobal::k_SimpleTypeID_Int);
	DOME_ASSERT(i_pParamList[1]->getDataPtr());

	Int l_Index = i_pParamList[1]->getDataPtr()->getInt();

    o_Size = i_pParamList[0]->getTexDataSize(l_Index);
    return R_SUCCESS;
}

/****************************
    FROM MDOperatorCpu class
****************************/
MDOperandPtr        MDOperatorArraySelector::execute(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, IRP* o_pInputReleasePoint) const
{
    PERF_COUNTER_EX(0);
	FRAMETIMER_BEGIN(FTT_RC_CAL_ARRAYSELECTOR, FTT_RC_CAL_EXECUTE_EFFECTPASS);
    DOME_ASSERT(i_ParamCount == 2);
	DOME_ASSERT(i_pParamList[1]->getDataType(0) == RCGlobal::k_SimpleTypeID_Int);
	DOME_ASSERT(i_pParamList[1]->getDataPtr());

	Int l_Index = i_pParamList[1]->getDataPtr()->getInt();

    o_pInputReleasePoint[0] = IRP_INFINISHCALLBACK;
    o_pInputReleasePoint[1] = IRP_AFTEREXECUTE;
	FRAMETIMER_END(FTT_RC_CAL_ARRAYSELECTOR);
	return i_pParamList[0]->getSubOperand(l_Index);
}

DResult             MDOperatorArraySelector::destroyResult(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, MDOperandPtr i_pResult) const
{
    return R_SUCCESS;
}


RC_NAMESPACE_END