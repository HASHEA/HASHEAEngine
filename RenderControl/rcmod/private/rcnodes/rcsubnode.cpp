#include "pch.h"
#include "../../public/rcmod.h"
#include "rcSubnode.h"

RC_NAMESPACE_BEGIN

RCEffectNode*    RCSubNode::Create(RCEffect* i_pEffect)
{
    return DOME_New(RCSubNode)(i_pEffect);
}

DResult          RCSubNode::Destroy(RCEffectNode* i_pEffectNode)
{
    DOME_Del(i_pEffectNode);
    return R_SUCCESS;
}

RCSubNode::RCSubNode(RCEffect* i_pEffect)
: RCEffectNode(i_pEffect)
{

}

RCSubNode::~RCSubNode()
{

}

// Main Work is Done Here.
DResult         RCSubNode::buildMDEffect(MDEffect* o_pStack, Int i_OutputSelector)
{
    DOME_ASSERT(i_OutputSelector == 0);

	// Set Input
    DResult l_Result;
    executePushInput(o_pStack, 0);
	executePushInput(o_pStack, 1);
	// Get and Set Value

	// SGet and Set Operation
    static const DStringHash k_MDOperator_MDOperatorName("MDSub");
    const MDOperator* l_pMDOperator = RCManager::Instance().getMDOperator(k_MDOperator_MDOperatorName);
    l_Result = o_pStack->pushOperatorGpu((const MDOperatorGpu*)l_pMDOperator);
    DOME_ASSERT(DM_SUCC(l_Result));

    return cacheResult(o_pStack, i_OutputSelector);
}

void            RCSubNode::finishLoad()
{

}


RC_NAMESPACE_END