#include "pch.h"
#include "../../public/rcmod.h"
#include "rcDivConstnode.h"

RC_NAMESPACE_BEGIN

RCEffectNode*    RCDivConstNode::Create(RCEffect* i_pEffect)
{
    return DOME_New(RCDivConstNode)(i_pEffect);
}

DResult          RCDivConstNode::Destroy(RCEffectNode* i_pEffectNode)
{
    DOME_Del(i_pEffectNode);
    return R_SUCCESS;
}

RCDivConstNode::RCDivConstNode(RCEffect* i_pEffect)
: RCEffectNode(i_pEffect)
, m_ConstValue(DM_NULL)
{

}

RCDivConstNode::~RCDivConstNode()
{

}

// Main Work is Done Here.
DResult         RCDivConstNode::buildMDEffect(MDEffect* o_pStack, Int i_OutputSelector)
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
    static const DStringHash k_MDOperator_MDOperatorName("MDDivConst");
    const MDOperator* l_pMDOperator = RCManager::Instance().getMDOperator(k_MDOperator_MDOperatorName);
    l_Result = o_pStack->pushOperatorGpu((const MDOperatorGpu*)l_pMDOperator);
    DOME_ASSERT(DM_SUCC(l_Result));

    return cacheResult(o_pStack, i_OutputSelector);
}

void            RCDivConstNode::finishLoad()
{

}


RC_NAMESPACE_END