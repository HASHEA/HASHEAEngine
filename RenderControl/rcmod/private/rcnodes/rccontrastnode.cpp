#include "pch.h"
#include "../../public/rcmod.h"
#include "rcContrastnode.h"

RC_NAMESPACE_BEGIN

RCEffectNode*    RCContrastNode::Create(RCEffect* i_pEffect)
{
    return DOME_New(RCContrastNode)(i_pEffect);
}

DResult          RCContrastNode::Destroy(RCEffectNode* i_pEffectNode)
{
    DOME_Del(i_pEffectNode);
    return R_SUCCESS;
}

RCContrastNode::RCContrastNode(RCEffect* i_pEffect)
: RCEffectNode(i_pEffect)
, m_ConstValue(DM_NULL)
{

}

RCContrastNode::~RCContrastNode()
{

}

// Main Work is Done Here.
DResult         RCContrastNode::buildMDEffect(MDEffect* o_pStack, Int i_OutputSelector)
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
    static const DStringHash k_MDOperator_MDOperatorName("MDContrast");
    const MDOperator* l_pMDOperator = RCManager::Instance().getMDOperator(k_MDOperator_MDOperatorName);
    l_Result = o_pStack->pushOperatorGpu((const MDOperatorGpu*)l_pMDOperator);
    DOME_ASSERT(DM_SUCC(l_Result));

    return cacheResult(o_pStack, i_OutputSelector);
}

void            RCContrastNode::finishLoad()
{

}


RC_NAMESPACE_END