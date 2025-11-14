#include "pch.h"
/*
    filename:       mdoperatormipmapgen.cpp
    author:         Ming Dong
    date:           2016-Aug-11
    description:    
*/

#include "mdoperatormipmapgen.h"
#include <rc/public/iexecuter.h>
#include <rc/public/mdoperation.h>

#ifdef RC_PERF
#include "KG3D_FrameTimer.h"
#else
#define FRAMETIMER_BEGIN(a,b)
#define FRAMETIMER_END(a)
#endif


RC_NAMESPACE_BEGIN

MDOperatorMipmapGen::MDOperatorMipmapGen()
    : m_OperatorName("MDMipmapGen")
{

}

MDOperatorMipmapGen::~MDOperatorMipmapGen()
{

}

/****************************
    FROM MDOperator class
****************************/
const DString&      MDOperatorMipmapGen::getOperatorName() const
{
    return m_OperatorName;
}

Bool                MDOperatorMipmapGen::isGpuOperator() const
{
    return DM_FALSE;
}

Int                 MDOperatorMipmapGen::getInputCount() const
{
    return 1;
}

DSimpleTypeID       MDOperatorMipmapGen::getInputTypeID(Int i_Index) const
{
    return RCGlobal::k_SimpleTypeID_OSTexture2D;
}

Int                 MDOperatorMipmapGen::getOutputCount() const
{
    return 1;
}

DSimpleTypeID       MDOperatorMipmapGen::getOutputTypeID(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_Index == 0);
    DOME_ASSERT(i_ParamCount == 1);
    return RCGlobal::k_SimpleTypeID_OSTexture2D;
}

RCGPUDATAFORMAT     MDOperatorMipmapGen::getOutputTexFmt(Int i_Index, MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_Index == 0);
    DOME_ASSERT(i_ParamCount == 1);
    if (getOutputTypeID(i_Index, i_pMDEffect, i_ParamCount, i_pParamList) == RCGlobal::k_SimpleTypeID_OSTexture2D)
    {
        DOME_ASSERT(i_pParamList[0] && i_pParamList[0]->isTexture());

        RCGPUDATAFORMAT l_Format;
        DResult l_Result = i_pParamList[0]->getTextureFormat(l_Format);
        DOME_ASSERT(DM_SUCC(l_Result));
        return l_Format;
    }
    else
        return RGDF_UNKNOWN;
}

DResult             MDOperatorMipmapGen::calcOutputTexSize(Int i_Index, MDEffect* i_pMDEffect, DVector2i& o_Size, Int i_ParamCount, const MDOperandCPtr* i_pParamList) const
{
    DOME_ASSERT(i_Index == 0);
    DOME_ASSERT(i_ParamCount == 1);
    DOME_ASSERT(i_pParamList[0]->isTexture());      // render target texture

    return i_pParamList[0]->getTextureSize(o_Size);
}

/****************************
    FROM MDOperatorCpu class
****************************/
MDOperandPtr        MDOperatorMipmapGen::execute(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, IRP* o_pInputReleasePoint) const
{
	FRAMETIMER_BEGIN(FTT_RC_CAL_MIPMAPGEN, FTT_RC_CAL_EXECUTE_EFFECTPASS);
    DOME_ASSERT(i_ParamCount == 1);
    
    RCEffectManager* l_pEffectMgr = i_pMDEffect->getRCEffect()->getEffectManager();
    RCRenderer* l_pRenderer = l_pEffectMgr->getRenderer();

    o_pInputReleasePoint[0] = IRP_AFTEREXECUTE;

    OSTexture2D l_SrcTex = *i_pParamList[0]->getTexturePtr();
    OSTexture2D l_ResultTex;

    DResult l_Result = l_pRenderer->generateMipmapsFromTexture2D(l_ResultTex, l_SrcTex);
    DOME_ASSERT(DM_SUCC(l_Result));

    MDOperandValue* l_pOperand = DOME_New(MDOperandValue)(RCGlobal::k_SimpleTypeID_OSTexture2D);
    l_Result = l_pOperand->getDataPtr()->set(RCGlobal::k_SimpleTypeID_OSTexture2D, &l_ResultTex);
    DOME_ASSERT(DM_SUCC(l_Result));

	FRAMETIMER_END(FTT_RC_CAL_MIPMAPGEN);

    return l_pOperand;
}

DResult             MDOperatorMipmapGen::destroyResult(MDEffect* i_pMDEffect, Int i_ParamCount, const MDOperandPtr* i_pParamList, MDOperandPtr i_pResult) const
{
    DOME_ASSERT(i_pResult->getTexturePtr());

    OSTexture2D l_ResultTex = *i_pResult->getTexturePtr();
    DOME_Del(i_pResult);

    RCEffectManager* l_pEffectMgr = i_pMDEffect->getRCEffect()->getEffectManager();
    RCRenderer* l_pRenderer = l_pEffectMgr->getRenderer();

    return l_pRenderer->destroyTexture2D(l_ResultTex);
}


RC_NAMESPACE_END