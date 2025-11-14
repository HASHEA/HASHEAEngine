#include "pch.h"
/*
    filename:       rcaddconstnode.cpp
    author:         Ming Dong
    date:           2016-JUN-27
    description:    
*/
#include "../../public/rcmod.h"
#include "rcaddconstnode.h"

RC_NAMESPACE_BEGIN

RCEffectNode*    RCAddConstNode::Create(RCEffect* i_pEffect)
{
    return DOME_New(RCAddConstNode)(i_pEffect);
}

DResult          RCAddConstNode::Destroy(RCEffectNode* i_pEffectNode)
{
    DOME_Del(i_pEffectNode);
    return R_SUCCESS;
}

RCAddConstNode::RCAddConstNode(RCEffect* i_pEffect)
: RCEffectNode(i_pEffect)
, m_ConstColorValue(DM_NULL)
{

}

RCAddConstNode::~RCAddConstNode()
{

}

DResult         RCAddConstNode::buildMDEffect(MDEffect* o_pStack, Int i_OutputSelector)
{
    DOME_ASSERT(i_OutputSelector == 0);

    DResult l_Result;
    executePushInput(o_pStack, 0);

    DSimpleTypedValue* l_pValue = getParam(0);
    m_ConstColorValue.set(l_pValue);
    o_pStack->pushOperand(&m_ConstColorValue);

    static const DStringHash k_MDOperator_MDAddConst("MDAddConst");
    const MDOperator* l_pMDAddConst = RCManager::Instance().getMDOperator(k_MDOperator_MDAddConst);
    l_Result = o_pStack->pushOperatorGpu((const MDOperatorGpu*)l_pMDAddConst);
    DOME_ASSERT(DM_SUCC(l_Result));

    return cacheResult(o_pStack, i_OutputSelector);
}

void            RCAddConstNode::finishLoad()
{

}


RC_NAMESPACE_END