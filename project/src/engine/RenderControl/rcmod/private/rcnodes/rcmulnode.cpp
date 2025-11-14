#include "pch.h"
#include "../../public/rcmod.h"
#include "rcMulnode.h"

RC_NAMESPACE_BEGIN

RCEffectNode*    RCMulNode::Create(RCEffect* i_pEffect)
{
    return DOME_New(RCMulNode)(i_pEffect);
}

DResult          RCMulNode::Destroy(RCEffectNode* i_pEffectNode)
{
    DOME_Del(i_pEffectNode);
    return R_SUCCESS;
}

RCMulNode::RCMulNode(RCEffect* i_pEffect)
: RCEffectNode(i_pEffect)
{

}

RCMulNode::~RCMulNode()
{

}

// Main Work is Done Here.
DResult         RCMulNode::buildMDEffect(MDEffect* o_pStack, Int i_OutputSelector)
{
    DOME_ASSERT(i_OutputSelector == 0);

	// Set Input
    DResult l_Result;
    executePushInput(o_pStack, 0);
	executePushInput(o_pStack, 1);

	// SGet and Set Operation
    static const DStringHash k_MDOperator_MDOperatorName("MDMul");
    const MDOperator* l_pMDOperator = RCManager::Instance().getMDOperator(k_MDOperator_MDOperatorName);
    l_Result = o_pStack->pushOperatorGpu((const MDOperatorGpu*)l_pMDOperator);
    DOME_ASSERT(DM_SUCC(l_Result));

    return cacheResult(o_pStack, i_OutputSelector);
}

void            RCMulNode::finishLoad()
{

}


RC_NAMESPACE_END