#include "pch.h"
#include "../../public/rcmod.h"
#include "rcDivnode.h"

RC_NAMESPACE_BEGIN

RCEffectNode*    RCDivNode::Create(RCEffect* i_pEffect)
{
    return DOME_New(RCDivNode)(i_pEffect);
}

DResult          RCDivNode::Destroy(RCEffectNode* i_pEffectNode)
{
    DOME_Del(i_pEffectNode);
    return R_SUCCESS;
}

RCDivNode::RCDivNode(RCEffect* i_pEffect)
: RCEffectNode(i_pEffect)
{

}

RCDivNode::~RCDivNode()
{

}

// Main Work is Done Here.
DResult         RCDivNode::buildMDEffect(MDEffect* o_pStack, Int i_OutputSelector)
{
    DOME_ASSERT(i_OutputSelector == 0);

	// Set Input
    DResult l_Result;
    executePushInput(o_pStack, 0);
	executePushInput(o_pStack, 1);
	
	// SGet and Set Operation
    static const DStringHash k_MDOperator_MDOperatorName("MDDiv");
    const MDOperator* l_pMDOperator = RCManager::Instance().getMDOperator(k_MDOperator_MDOperatorName);
    l_Result = o_pStack->pushOperatorGpu((const MDOperatorGpu*)l_pMDOperator);
    DOME_ASSERT(DM_SUCC(l_Result));

    return cacheResult(o_pStack, i_OutputSelector);
}

void            RCDivNode::finishLoad()
{

}


RC_NAMESPACE_END