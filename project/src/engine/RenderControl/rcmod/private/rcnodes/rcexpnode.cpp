#include "pch.h"
#include "../../public/rcmod.h"
#include "rcExpnode.h"

RC_NAMESPACE_BEGIN

RCEffectNode*    RCExpNode::Create(RCEffect* i_pEffect)
{
    return DOME_New(RCExpNode)(i_pEffect);
}

DResult          RCExpNode::Destroy(RCEffectNode* i_pEffectNode)
{
    DOME_Del(i_pEffectNode);
    return R_SUCCESS;
}

RCExpNode::RCExpNode(RCEffect* i_pEffect)
: RCEffectNode(i_pEffect)
{

}

RCExpNode::~RCExpNode()
{

}

// Main Work is Done Here.
DResult         RCExpNode::buildMDEffect(MDEffect* o_pStack, Int i_OutputSelector)
{
    DOME_ASSERT(i_OutputSelector == 0);

	// Set Input
    DResult l_Result;
    executePushInput(o_pStack, 0);

	// SGet and Set Operation
    static const DStringHash k_MDOperator_MDOperatorName("MDExp");
    const MDOperator* l_pMDOperator = RCManager::Instance().getMDOperator(k_MDOperator_MDOperatorName);
    l_Result = o_pStack->pushOperatorGpu((const MDOperatorGpu*)l_pMDOperator);
    DOME_ASSERT(DM_SUCC(l_Result));

    return cacheResult(o_pStack, i_OutputSelector);
}

void            RCExpNode::finishLoad()
{

}


RC_NAMESPACE_END