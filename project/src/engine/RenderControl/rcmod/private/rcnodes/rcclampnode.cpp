#include "pch.h"
#include "../../public/rcmod.h"
#include "rcclampnode.h"

RC_NAMESPACE_BEGIN

RCEffectNode*    RCClampNode::Create(RCEffect* i_pEffect)
{
    return DOME_New(RCClampNode)(i_pEffect);
}

DResult          RCClampNode::Destroy(RCEffectNode* i_pEffectNode)
{
    DOME_Del(i_pEffectNode);
    return R_SUCCESS;
}

RCClampNode::RCClampNode(RCEffect* i_pEffect)
: RCEffectNode(i_pEffect)
, m_ConstValue(DM_NULL)
, m_ConstValue1(DM_NULL)
{

}

RCClampNode::~RCClampNode()
{

}

// Main Work is Done Here.
DResult         RCClampNode::buildMDEffect(MDEffect* o_pStack, Int i_OutputSelector)
{
    DOME_ASSERT(i_OutputSelector == 0);

	// Set Input
    DResult l_Result;
    executePushInput(o_pStack, 0);

	// Get and Set Value
	
    DSimpleTypedValue* l_pValue = getParam(0);
    m_ConstValue.set(l_pValue);
    o_pStack->pushOperand(&m_ConstValue);
	
	DSimpleTypedValue* l_pValue1 = getParam(1);
    m_ConstValue1.set(l_pValue1);
    o_pStack->pushOperand(&m_ConstValue1);
	

	// SGet and Set Operation
    static const DStringHash k_MDOperator_MDOperatorName("MDClamp");
    const MDOperator* l_pMDOperator = RCManager::Instance().getMDOperator(k_MDOperator_MDOperatorName);
    l_Result = o_pStack->pushOperatorGpu((const MDOperatorGpu*)l_pMDOperator);
    DOME_ASSERT(DM_SUCC(l_Result));

    return cacheResult(o_pStack, i_OutputSelector);
}

void            RCClampNode::finishLoad()
{

}


RC_NAMESPACE_END