#include "pch.h"
/*
    filename:       mdoperatorcopyvalueto.cpp
    author:         Ming Dong
    date:           2016-JUN-28
    description:    
*/

#include "mdoperatorfloatlerp.h"
#include <rc/public/iexecuter.h>

#ifdef RC_PERF
#include "KG3D_FrameTimer.h"
#else
#define FRAMETIMER_BEGIN(a,b)
#define FRAMETIMER_END(a)
#endif


RC_NAMESPACE_BEGIN

MDOperatorFloatLerp::MDOperatorFloatLerp()
    : m_OperatorName("MDFloatLerp")
{

}

MDOperatorFloatLerp::~MDOperatorFloatLerp()
{

}

/****************************
    FROM MDOperator class
****************************/
const DString&      MDOperatorFloatLerp::getOperatorName() const
{
    return m_OperatorName;
}

Bool                MDOperatorFloatLerp::isGpuOperator() const
{
    return DM_FALSE;
}

Int                 MDOperatorFloatLerp::getInputCount() const
{
    return 3;
}

DSimpleTypeID       MDOperatorFloatLerp::getInputTypeID(Int i_Index) const
{
    return RCGlobal::k_SimpleTypeID_Unknown;
}

Int                 MDOperatorFloatLerp::getOutputCount() const
{
    return 1;
}

DSimpleTypeID       MDOperatorFloatLerp::getOutputTypeID(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_Index == 0);
    DOME_ASSERT(i_ParamCount == 3);
    DOME_ASSERT(i_pParamList[0]->getDataCount() == 1);
    return i_pParamList[0]->getDataType(0);
}

RCGPUDATAFORMAT     MDOperatorFloatLerp::getOutputTexFmt(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_Index == 0);
    DOME_ASSERT(i_ParamCount == 3);
    
    return RGDF_UNKNOWN;
}

DResult             MDOperatorFloatLerp::calcOutputTexSize(Int i_Index, MDEffect* i_pMDEffect, DVector2i& o_Size, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    return i_pParamList[0]->getTextureSize(o_Size);
}

/****************************
    FROM MDOperatorCpu class
****************************/
MDOperandPtr        MDOperatorFloatLerp::execute(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, IRP* o_pInputReleasePoint) const
{
	FRAMETIMER_BEGIN(FTT_RC_CAL_FLOATLERP, FTT_RC_CAL_EXECUTE_EFFECTPASS);
	DOME_ASSERT(i_ParamCount == 3);
	DOME_ASSERT(i_pParamList[0]);      // source value
	DOME_ASSERT(i_pParamList[0]->getDataCount() == 1);
	DOME_ASSERT(i_pParamList[0]->getDataType(0) == i_pParamList[1]->getDataType(0));
	DOME_ASSERT(i_pParamList[0]->getDataType(0) == i_pParamList[2]->getDataType(0));

    o_pInputReleasePoint[0] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[1] = IRP_AFTEREXECUTE;
	o_pInputReleasePoint[2] = IRP_AFTEREXECUTE;

	DSimpleTypeID l_TypeID = i_pParamList[0]->getDataType(0);
	DSimpleTypedValue* l_pSrc = i_pParamList[0]->getDataPtr();   
	DOME_ASSERT(l_pSrc);

	DSimpleTypedValue* l_pSrc2 = i_pParamList[1]->getDataPtr();   
	DOME_ASSERT(l_pSrc2);

	DSimpleTypedValue* l_pFactor = i_pParamList[2]->getDataPtr();   
	DOME_ASSERT(l_pFactor);

	if (l_TypeID == RCGlobal::k_SimpleTypeID_F32)									
	{
		F32 l_fSrc = l_pSrc->getF32();
		F32 l_fSrc2 = l_pSrc2->getF32();
		F32 l_fFactor = l_pFactor->getF32();

		F32 l_ResultF32 = l_fSrc + l_fFactor * ( l_fSrc2 - l_fSrc);
		MDOperandValue* l_pOperand = DOME_New(MDOperandValue)(RCGlobal::k_SimpleTypeID_F32);
		l_pOperand->getDataPtr()->setF32(l_ResultF32);
        FRAMETIMER_END(FTT_RC_CAL_FLOATLERP);
		return l_pOperand;
	}
	else if (l_TypeID == RCGlobal::k_SimpleTypeID_DVector2f)						
	{
		DVector2f l_SrcVec2 = l_pSrc->getDVector2f();
		DVector2f l_Src2Vec2 = l_pSrc2->getDVector2f();
		DVector2f l_FactorVec2 = l_pFactor->getDVector2f();

		DVector2f l_ResultVec2 = DVector2f(l_SrcVec2.x + l_FactorVec2.x * (l_Src2Vec2.x - l_SrcVec2.x), 
										   l_SrcVec2.y + l_FactorVec2.y * (l_Src2Vec2.y - l_SrcVec2.y));
		MDOperandValue* l_pOperand = DOME_New(MDOperandValue)(RCGlobal::k_SimpleTypeID_DVector2f);
		l_pOperand->getDataPtr()->setDVector2f(l_ResultVec2);
		FRAMETIMER_END(FTT_RC_CAL_FLOATLERP);
        return l_pOperand;
	}
	else if (l_TypeID == RCGlobal::k_SimpleTypeID_DVector3f)						
	{
		DVector3f l_SrcVec3 = l_pSrc->getDVector3f();
		DVector3f l_Src2Vec3 = l_pSrc2->getDVector3f();
		DVector3f l_FactorVec3 = l_pFactor->getDVector3f();

		DVector3f l_ResultVec3 = DVector3f(
			l_SrcVec3.x + l_FactorVec3.x * (l_Src2Vec3.x - l_SrcVec3.x), 
			l_SrcVec3.y + l_FactorVec3.y * (l_Src2Vec3.y - l_SrcVec3.y), 
			l_SrcVec3.z + l_FactorVec3.z * (l_Src2Vec3.z - l_SrcVec3.z));
		MDOperandValue* l_pOperand = DOME_New(MDOperandValue)(RCGlobal::k_SimpleTypeID_DVector3f);
		l_pOperand->getDataPtr()->setDVector3f(l_ResultVec3);
		FRAMETIMER_END(FTT_RC_CAL_FLOATLERP);
        return l_pOperand;
	}
	else if (l_TypeID == RCGlobal::k_SimpleTypeID_DVector4f)						
	{
		DVector4f l_SrcVec4 = l_pSrc->getDVector4f();
		DVector4f l_Src2Vec4 = l_pSrc2->getDVector4f();
		DVector4f l_FactorVec4 = l_pFactor->getDVector4f();

		DVector4f l_ResultVec4 = l_SrcVec4 + l_FactorVec4 * (l_Src2Vec4 - l_SrcVec4);

		MDOperandValue* l_pOperand = DOME_New(MDOperandValue)(RCGlobal::k_SimpleTypeID_DVector4f);
		l_pOperand->getDataPtr()->setDVector4f(l_ResultVec4);
		FRAMETIMER_END(FTT_RC_CAL_FLOATLERP);
        return l_pOperand;
	}
	FRAMETIMER_END(FTT_RC_CAL_FLOATLERP);
	return NULL;
}

DResult             MDOperatorFloatLerp::destroyResult(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, MDOperandPtr i_pResult) const
{
	DOME_Del(i_pResult);
	return R_SUCCESS;
}


RC_NAMESPACE_END