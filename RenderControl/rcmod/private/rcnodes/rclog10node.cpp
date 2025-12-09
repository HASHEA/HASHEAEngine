#include "pch.h"
#include "../../public/rcmod.h"
#include "rcLog10node.h"

RC_NAMESPACE_BEGIN

RCEffectNode*    RCLog10Node::Create(RCEffect* i_pEffect)
{
    return DOME_New(RCLog10Node)(i_pEffect);
}

DResult          RCLog10Node::Destroy(RCEffectNode* i_pEffectNode)
{
    DOME_Del(i_pEffectNode);
    return R_SUCCESS;
}

RCLog10Node::RCLog10Node(RCEffect* i_pEffect)
: RCEffectNode(i_pEffect)
{

}

RCLog10Node::~RCLog10Node()
{

}

// Main Work is Done Here.
DResult         RCLog10Node::buildMDEffect(MDEffect* o_pStack, Int i_OutputSelector)
{
    DOME_ASSERT(i_OutputSelector == 0);

	// Set Input
    DResult l_Result;
    executePushInput(o_pStack, 0);

	// SGet and Set Operation
    static const DStringHash k_MDOperator_MDOperatorName("MDLog10");
    const MDOperator* l_pMDOperator = RCManager::Instance().getMDOperator(k_MDOperator_MDOperatorName);
    l_Result = o_pStack->pushOperatorGpu((const MDOperatorGpu*)l_pMDOperator);
    DOME_ASSERT(DM_SUCC(l_Result));

    return cacheResult(o_pStack, i_OutputSelector);
}

void            RCLog10Node::finishLoad()
{

}


RC_NAMESPACE_END