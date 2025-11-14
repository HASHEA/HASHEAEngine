#include "pch.h"
#include "../../public/rcmod.h"
#include "rcAddnode.h"

RC_NAMESPACE_BEGIN

RCEffectNode*    RCAddNode::Create(RCEffect* i_pEffect)
{
    return DOME_New(RCAddNode)(i_pEffect);
}

DResult          RCAddNode::Destroy(RCEffectNode* i_pEffectNode)
{
    DOME_Del(i_pEffectNode);
    return R_SUCCESS;
}

RCAddNode::RCAddNode(RCEffect* i_pEffect)
: RCEffectNode(i_pEffect)
/*, m_ConstValue(DM_NULL)*/
{

}

RCAddNode::~RCAddNode()
{

}

// Main Work is Done Here.
DResult         RCAddNode::buildMDEffect(MDEffect* o_pStack, Int i_OutputSelector)
{
    DOME_ASSERT(i_OutputSelector == 0);

	// Set Input
    DResult l_Result;
    executePushInput(o_pStack, 0);
	executePushInput(o_pStack, 1);
	
	// Get and Set Value
	/*
    DSimpleTypedValue* l_pValue = getParam(0);
    m_ConstColorValue.set(l_pValue);
    o_pStack->pushOperand(&m_ConstColorValue);
	*/

	// SGet and Set Operation
    static const DStringHash k_MDOperator_MDOperatorName("MDAdd");
    const MDOperator* l_pMDOperator = RCManager::Instance().getMDOperator(k_MDOperator_MDOperatorName);
    l_Result = o_pStack->pushOperatorGpu((const MDOperatorGpu*)l_pMDOperator);
    DOME_ASSERT(DM_SUCC(l_Result));

    return cacheResult(o_pStack, i_OutputSelector);
}

void            RCAddNode::finishLoad()
{

}


RC_NAMESPACE_END