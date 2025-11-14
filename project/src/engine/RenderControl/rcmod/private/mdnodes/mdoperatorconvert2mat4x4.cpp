#include "pch.h"
/*
    filename:       MDOperatorConvert2Mat4x4.cppMDOperatorConvert2Mat4x4
    author:         Ming Dong
    date:           2016-JUN-28
    description:    
*/

#include "MDOperatorConvert2Mat4x4.h"
#include <rc/public/iexecuter.h>

#ifdef RC_PERF
#include "KG3D_FrameTimer.h"
#else
#define FRAMETIMER_BEGIN(a,b)
#define FRAMETIMER_END(a)
#endif


RC_NAMESPACE_BEGIN

MDOperatorConvert2Mat4x4::MDOperatorConvert2Mat4x4()
    : m_OperatorName("MDConvert2Mat4x4")
{

}

MDOperatorConvert2Mat4x4::~MDOperatorConvert2Mat4x4()
{

}

/****************************
    FROM MDOperator class
****************************/
const DString&      MDOperatorConvert2Mat4x4::getOperatorName() const
{
    return m_OperatorName;
}

Bool                MDOperatorConvert2Mat4x4::isGpuOperator() const
{
    return DM_FALSE;
}

Int                 MDOperatorConvert2Mat4x4::getInputCount() const
{
    return 1;
}

DSimpleTypeID       MDOperatorConvert2Mat4x4::getInputTypeID(Int i_Index) const
{
    return RCGlobal::k_SimpleTypeID_Unknown;
}

Int                 MDOperatorConvert2Mat4x4::getOutputCount() const
{
    return 1;
}

DSimpleTypeID       MDOperatorConvert2Mat4x4::getOutputTypeID(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_Index == 0);
    DOME_ASSERT(i_ParamCount == 1);
    DOME_ASSERT(i_pParamList[0]->getDataCount() == 1);
    return RCGlobal::k_SimpleTypeID_DMatrix4x4f; /*i_pParamList[0]->getDataType(0);*/
}

RCGPUDATAFORMAT     MDOperatorConvert2Mat4x4::getOutputTexFmt(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_Index == 0);
    DOME_ASSERT(i_ParamCount == 1);
	return RGDF_UNKNOWN;
}

DResult             MDOperatorConvert2Mat4x4::calcOutputTexSize(Int i_Index, MDEffect* i_pMDEffect, DVector2i& o_Size, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_Index == 0);
    DOME_ASSERT(i_ParamCount == 1);

    return FALSE;
}

/****************************
    FROM MDOperatorCpu class
****************************/
MDOperandPtr        MDOperatorConvert2Mat4x4::execute(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, IRP* o_pInputReleasePoint) const
{
    PERF_COUNTER_EX(0);
	FRAMETIMER_BEGIN(FTT_RC_CAL_C2M44, FTT_RC_CAL_EXECUTE_EFFECTPASS);
    DOME_ASSERT(i_ParamCount == 1);
    DOME_ASSERT(i_pParamList[0]);      // source value
      
    o_pInputReleasePoint[0] = IRP_INFINISHCALLBACK;

    DSimpleTypedValue* l_pSrc = i_pParamList[0]->getDataPtr();   
    DOME_ASSERT(l_pSrc);
    DOME_ASSERT(l_pSrc->getTypeID() == RCGlobal::k_SimpleTypeID_DMatrix4x4f);
		FRAMETIMER_END(FTT_RC_CAL_C2M44);
    return i_pParamList[0];
}

DResult             MDOperatorConvert2Mat4x4::destroyResult(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, MDOperandPtr i_pResult) const
{
	return R_SUCCESS;
}

RC_NAMESPACE_END