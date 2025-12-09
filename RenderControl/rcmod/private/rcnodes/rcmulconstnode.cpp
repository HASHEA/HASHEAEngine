#include "pch.h"
#include "../../public/rcmod.h"
#include "rcMulConstnode.h"

RC_NAMESPACE_BEGIN

RCEffectNode*    RCMulConstNode::Create(RCEffect* i_pEffect)
{
    return DOME_New(RCMulConstNode)(i_pEffect);
}

DResult          RCMulConstNode::Destroy(RCEffectNode* i_pEffectNode)
{
    DOME_Del(i_pEffectNode);
    return R_SUCCESS;
}

RCMulConstNode::RCMulConstNode(RCEffect* i_pEffect)
: RCEffectNode(i_pEffect)
, m_ConstValue(DM_NULL)
{

}

RCMulConstNode::~RCMulConstNode()
{

}

// Main Work is Done Here.
DResult         RCMulConstNode::buildMDEffect(MDEffect* o_pStack, Int i_OutputSelector)
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
    static const DStringHash k_MDOperator_MDOperatorName("MDMulConst");
    const MDOperator* l_pMDOperator = RCManager::Instance().getMDOperator(k_MDOperator_MDOperatorName);
    l_Result = o_pStack->pushOperatorGpu((const MDOperatorGpu*)l_pMDOperator);
    DOME_ASSERT(DM_SUCC(l_Result));

    return cacheResult(o_pStack, i_OutputSelector);
}

void            RCMulConstNode::finishLoad()
{

}


RC_NAMESPACE_END