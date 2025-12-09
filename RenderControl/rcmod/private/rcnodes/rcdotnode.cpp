#include "pch.h"
#include "../../public/rcmod.h"
#include "rcDotnode.h"

RC_NAMESPACE_BEGIN

RCEffectNode*    RCDotNode::Create(RCEffect* i_pEffect)
{
    return DOME_New(RCDotNode)(i_pEffect);
}

DResult          RCDotNode::Destroy(RCEffectNode* i_pEffectNode)
{
    DOME_Del(i_pEffectNode);
    return R_SUCCESS;
}

RCDotNode::RCDotNode(RCEffect* i_pEffect)
: RCEffectNode(i_pEffect)
{

}

RCDotNode::~RCDotNode()
{

}

// Main Work is Done Here.
DResult         RCDotNode::buildMDEffect(MDEffect* o_pStack, Int i_OutputSelector)
{
    DOME_ASSERT(i_OutputSelector == 0);

	// Set Input
    DResult l_Result;
    executePushInput(o_pStack, 0);
	executePushInput(o_pStack, 1);

	// SGet and Set Operation
    static const DStringHash k_MDOperator_MDOperatorName("MDDot");
    const MDOperator* l_pMDOperator = RCManager::Instance().getMDOperator(k_MDOperator_MDOperatorName);
    l_Result = o_pStack->pushOperatorGpu((const MDOperatorGpu*)l_pMDOperator);
    DOME_ASSERT(DM_SUCC(l_Result));

    return cacheResult(o_pStack, i_OutputSelector);
}

void            RCDotNode::finishLoad()
{

}


RC_NAMESPACE_END