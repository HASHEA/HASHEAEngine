#include "pch.h"
/*
    filename:       rctonemappingnode.cpp
    author:         Ming Dong
    date:           2016-Aug-11
    description:    
*/
#include "../../public/rcmod.h"
#include "rctonemappingnode.h"

RC_NAMESPACE_BEGIN

RCEffectNode*    RCToneMappingNode::Create(RCEffect* i_pEffect)
{
    return DOME_New(RCToneMappingNode)(i_pEffect);
}

DResult          RCToneMappingNode::Destroy(RCEffectNode* i_pEffectNode)
{
    DOME_Del(i_pEffectNode);
    return R_SUCCESS;
}

RCToneMappingNode::RCToneMappingNode(RCEffect* i_pEffect)
: RCEffectNode(i_pEffect)
, m_LumRangeLeftValue(DM_NULL)
, m_LumRangeRightValue(DM_NULL)
{

}

RCToneMappingNode::~RCToneMappingNode()
{

}

DResult         RCToneMappingNode::buildMDEffect(MDEffect* o_pStack, Int i_OutputSelector)
{
    DOME_ASSERT(i_OutputSelector == 0);
    static const DStringHash k_MDOperator_MDLumAdjust("MDLumAdjust");
    const MDOperator* l_pMDLumAdjust = RCManager::Instance().getMDOperator(k_MDOperator_MDLumAdjust);

    DResult l_Result;
    executePushInput(o_pStack, 0);
    executePushInput(o_pStack, 1);


    DOME_ASSERT(getParamType(0) == RCGlobal::k_SimpleTypeID_F32);
    DOME_ASSERT(getParamType(1) == RCGlobal::k_SimpleTypeID_F32);
    m_LumRangeLeftValue.set(getParam(0));
    o_pStack->pushOperand(&m_LumRangeLeftValue);
    m_LumRangeRightValue.set(getParam(1));
    o_pStack->pushOperand(&m_LumRangeRightValue);

    l_Result = o_pStack->pushOperatorGpu((const MDOperatorGpu*)l_pMDLumAdjust);
    DOME_ASSERT(DM_SUCC(l_Result));

    return cacheResult(o_pStack, i_OutputSelector);
}

void            RCToneMappingNode::finishLoad()
{

}


RC_NAMESPACE_END