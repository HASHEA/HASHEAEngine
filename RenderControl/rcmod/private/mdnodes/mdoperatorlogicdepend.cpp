#include "pch.h"
/*
    filename:       mdoperatorlogicdepend.cpp
    author:         Ming Dong
    date:           2016-Aug-09
    description:    
*/

#include "mdoperatorlogicdepend.h"
#include <rc/public/iexecuter.h>
#include <rc/public/mdoperation.h>

#ifdef RC_PERF
#include "KG3D_FrameTimer.h"
#else
#define FRAMETIMER_BEGIN(a,b)
#define FRAMETIMER_END(a)
#endif


RC_NAMESPACE_BEGIN

MDOperatorLogicDepend::MDOperatorLogicDepend()
    : m_OperatorName("MDLogicDepend")
{

}

MDOperatorLogicDepend::~MDOperatorLogicDepend()
{

}

/****************************
    FROM MDOperator class
****************************/
const DString&      MDOperatorLogicDepend::getOperatorName() const
{
    return m_OperatorName;
}

Bool                MDOperatorLogicDepend::isGpuOperator() const
{
    return DM_FALSE;
}

Int                 MDOperatorLogicDepend::getInputCount() const
{
    return 2;
}

DSimpleTypeID       MDOperatorLogicDepend::getInputTypeID(Int i_Index) const
{
    return RCGlobal::k_SimpleTypeID_Unknown;
}

DSimpleTypeID       MDOperatorLogicDepend::getOutputTypeID(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_Index == 0);
    DOME_ASSERT(i_ParamCount == 2);
    DOME_ASSERT(i_pParamList[0]->getDataCount() == 1);
    return i_pParamList[1]->getDataType(0);
}

Int                 MDOperatorLogicDepend::getOutputCount() const
{
    return 1;
}

RCGPUDATAFORMAT     MDOperatorLogicDepend::getOutputTexFmt(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_Index == 0);
    DOME_ASSERT(i_ParamCount == 2);
    if (getOutputTypeID(i_Index, i_pMDEffect, i_ParamCount, i_pParamList) == RCGlobal::k_SimpleTypeID_OSTexture2D)
    {
        DOME_ASSERT(i_pParamList[1] && i_pParamList[1]->isTexture());

        RCGPUDATAFORMAT l_Format;
        DResult l_Result = i_pParamList[1]->getTextureFormat(l_Format);
        DOME_ASSERT(DM_SUCC(l_Result));
        return l_Format;
    }
    else
        return RGDF_UNKNOWN;
}

DResult             MDOperatorLogicDepend::calcOutputTexSize(Int i_Index, MDEffect* i_pMDEffect, DVector2i& o_Size, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_Index == 0);
    DOME_ASSERT(i_ParamCount == 2);
    DOME_ASSERT(i_pParamList[1]->isTexture());      // render target texture

    return i_pParamList[1]->getTextureSize(o_Size);
}

/****************************
    FROM MDOperatorCpu class
****************************/
MDOperandPtr        MDOperatorLogicDepend::execute(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, IRP* o_pInputReleasePoint) const
{
	FRAMETIMER_BEGIN(FTT_RC_CAL_LOGICDEPEND, FTT_RC_CAL_EXECUTE_EFFECTPASS);
    DOME_ASSERT(i_ParamCount == 2);
    
    RCEffectManager* l_pEffectMgr = i_pMDEffect->getRCEffect()->getEffectManager();
    RCRenderer* l_pRenderer = l_pEffectMgr->getRenderer();

    o_pInputReleasePoint[0] = IRP_AFTEREXECUTE;
    o_pInputReleasePoint[1] = IRP_INFINISHCALLBACK;

	FRAMETIMER_END(FTT_RC_CAL_LOGICDEPEND);

    return i_pParamList[1];
}

DResult             MDOperatorLogicDepend::destroyResult(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, MDOperandPtr i_pResult) const
{
    return R_SUCCESS;
}


RC_NAMESPACE_END