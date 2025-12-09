#include "pch.h"
#include "../../public/rcmod.h"
#include "rcPownode.h"

RC_NAMESPACE_BEGIN

RCEffectNode*    RCPowNode::Create(RCEffect* i_pEffect)
{
    return DOME_New(RCPowNode)(i_pEffect);
}

DResult          RCPowNode::Destroy(RCEffectNode* i_pEffectNode)
{
    DOME_Del(i_pEffectNode);
    return R_SUCCESS;
}

RCPowNode::RCPowNode(RCEffect* i_pEffect)
: RCEffectNode(i_pEffect)
, m_ConstValue(DM_NULL)
{

}

RCPowNode::~RCPowNode()
{

}

// Main Work is Done Here.
DResult         RCPowNode::buildMDEffect(MDEffect* o_pStack, Int i_OutputSelector)
{
    DOME_ASSERT(i_OutputSelector == 0);

	// Set Input
    DResult l_Result;
    executePushInput(o_pStack, 0);

	// Get and Set Value
	
    DSimpleTypedValue* l_pValue = getParam(0);
    m_ConstValue.set(l_pValue);
    o_pStack->pushOperand(&m_ConstValue);

	// SGet and Set Operation
    static const DStringHash k_MDOperator_MDOperatorName("MDPow");
    const MDOperator* l_pMDOperator = RCManager::Instance().getMDOperator(k_MDOperator_MDOperatorName);
    l_Result = o_pStack->pushOperatorGpu((const MDOperatorGpu*)l_pMDOperator);
    DOME_ASSERT(DM_SUCC(l_Result));

    return cacheResult(o_pStack, i_OutputSelector);
}

void            RCPowNode::finishLoad()
{

}


RC_NAMESPACE_END