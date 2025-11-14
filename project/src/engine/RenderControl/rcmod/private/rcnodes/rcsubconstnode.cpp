#include "pch.h"
#include "../../public/rcmod.h"
#include "rcSubConstnode.h"

RC_NAMESPACE_BEGIN

RCEffectNode*    RCSubConstNode::Create(RCEffect* i_pEffect)
{
    return DOME_New(RCSubConstNode)(i_pEffect);
}

DResult          RCSubConstNode::Destroy(RCEffectNode* i_pEffectNode)
{
    DOME_Del(i_pEffectNode);
    return R_SUCCESS;
}

RCSubConstNode::RCSubConstNode(RCEffect* i_pEffect)
: RCEffectNode(i_pEffect)
, m_ConstValue(DM_NULL)
{

}

RCSubConstNode::~RCSubConstNode()
{

}

// Main Work is Done Here.
DResult         RCSubConstNode::buildMDEffect(MDEffect* o_pStack, Int i_OutputSelector)
{
    DOME_ASSERT(i_OutputSelector == 0);

	DResult l_Result;
	executePushInput(o_pStack, 0);
	
    DSimpleTypedValue* l_pValue = getParam(0);
	m_ConstValue.set(l_pValue);
	o_pStack->pushOperand(&m_ConstValue);	

	// SGet and Set Operation
    static const DStringHash k_MDOperator_MDOperatorName("MDSubConst");
    const MDOperator* l_pMDOperator = RCManager::Instance().getMDOperator(k_MDOperator_MDOperatorName);
    l_Result = o_pStack->pushOperatorGpu((const MDOperatorGpu*)l_pMDOperator);
    DOME_ASSERT(DM_SUCC(l_Result));

    return cacheResult(o_pStack, i_OutputSelector);
}

void            RCSubConstNode::finishLoad()
{

}


RC_NAMESPACE_END