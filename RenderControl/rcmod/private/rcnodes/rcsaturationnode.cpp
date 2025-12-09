#include "pch.h"
#include "../../public/rcmod.h"
#include "rcSaturationnode.h"

RC_NAMESPACE_BEGIN

RCEffectNode*    RCSaturationNode::Create(RCEffect* i_pEffect)
{
    return DOME_New(RCSaturationNode)(i_pEffect);
}

DResult          RCSaturationNode::Destroy(RCEffectNode* i_pEffectNode)
{
    DOME_Del(i_pEffectNode);
    return R_SUCCESS;
}

RCSaturationNode::RCSaturationNode(RCEffect* i_pEffect)
: RCEffectNode(i_pEffect)
, m_ConstValue(DM_NULL)
{

}

RCSaturationNode::~RCSaturationNode()
{

}

// Main Work is Done Here.
DResult         RCSaturationNode::buildMDEffect(MDEffect* o_pStack, Int i_OutputSelector)
{
    DOME_ASSERT(i_OutputSelector == 0);

	// Set Input
    DResult l_Result;
	
	// RGB to HLS
    executePushInput(o_pStack, 0);
	
	static const DStringHash k_MDOperator_MDOperatorName1("MDRGB2HLS");
    const MDOperator* l_pMDOperator = RCManager::Instance().getMDOperator(k_MDOperator_MDOperatorName1);
    l_Result = o_pStack->pushOperatorGpu((const MDOperatorGpu*)l_pMDOperator);
    DOME_ASSERT(DM_SUCC(l_Result));
	
	// Saturation
	// Get and Set Value
    DSimpleTypedValue* l_pValue = getParam(0);
    m_ConstValue.set(l_pValue);
    o_pStack->pushOperand(&m_ConstValue);
	
	// SGet and Set Operation
    static const DStringHash k_MDOperator_MDOperatorName2("MDSaturation");
    l_pMDOperator = RCManager::Instance().getMDOperator(k_MDOperator_MDOperatorName2);
    l_Result = o_pStack->pushOperatorGpu((const MDOperatorGpu*)l_pMDOperator);
    DOME_ASSERT(DM_SUCC(l_Result));

	// HLS to RGB
	static const DStringHash k_MDOperator_MDOperatorName3("MDHLS2RGB");
    l_pMDOperator = RCManager::Instance().getMDOperator(k_MDOperator_MDOperatorName3);
    l_Result = o_pStack->pushOperatorGpu((const MDOperatorGpu*)l_pMDOperator);
    DOME_ASSERT(DM_SUCC(l_Result));
	
    return cacheResult(o_pStack, i_OutputSelector);
}

void            RCSaturationNode::finishLoad()
{

}


RC_NAMESPACE_END