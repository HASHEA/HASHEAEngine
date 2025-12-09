#include "pch.h"
#include "../../public/rcmod.h"
#include "rcLog2node.h"

RC_NAMESPACE_BEGIN

RCEffectNode*    RCLog2Node::Create(RCEffect* i_pEffect)
{
    return DOME_New(RCLog2Node)(i_pEffect);
}

DResult          RCLog2Node::Destroy(RCEffectNode* i_pEffectNode)
{
    DOME_Del(i_pEffectNode);
    return R_SUCCESS;
}

RCLog2Node::RCLog2Node(RCEffect* i_pEffect)
: RCEffectNode(i_pEffect)
{

}

RCLog2Node::~RCLog2Node()
{

}

// Main Work is Done Here.
DResult         RCLog2Node::buildMDEffect(MDEffect* o_pStack, Int i_OutputSelector)
{
    DOME_ASSERT(i_OutputSelector == 0);

	// Set Input
    DResult l_Result;
    executePushInput(o_pStack, 0);

	// SGet and Set Operation
    static const DStringHash k_MDOperator_MDOperatorName("MDLog2");
    const MDOperator* l_pMDOperator = RCManager::Instance().getMDOperator(k_MDOperator_MDOperatorName);
    l_Result = o_pStack->pushOperatorGpu((const MDOperatorGpu*)l_pMDOperator);
    DOME_ASSERT(DM_SUCC(l_Result));

    return cacheResult(o_pStack, i_OutputSelector);
}

void            RCLog2Node::finishLoad()
{

}


RC_NAMESPACE_END