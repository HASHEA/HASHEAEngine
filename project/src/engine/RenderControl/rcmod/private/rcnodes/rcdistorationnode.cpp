#include "pch.h"
/*
    filename:       rcdistorationnode.cpp
    author:         Ming Dong
    date:           2016-Aug-24
    description:    
*/
#include "../../public/rcmod.h"
#include "rcdistorationnode.h"

RC_NAMESPACE_BEGIN

RCEffectNode*    RCDistorationNode::Create(RCEffect* i_pEffect)
{
    return DOME_New(RCDistorationNode)(i_pEffect);
}

DResult          RCDistorationNode::Destroy(RCEffectNode* i_pEffectNode)
{
    DOME_Del(i_pEffectNode);
    return R_SUCCESS;
}

RCDistorationNode::RCDistorationNode(RCEffect* i_pEffect)
: RCEffectNode(i_pEffect)
{

}

RCDistorationNode::~RCDistorationNode()
{

}

DResult         RCDistorationNode::buildMDEffect(MDEffect* o_pStack, Int i_OutputSelector)
{
    DOME_ASSERT(i_OutputSelector == 0);

    DResult l_Result;

    executePushInput(o_pStack, 0);
    executePushInput(o_pStack, 1);

    DSimpleTypedValue* l_pValue = getParam(0);
    DOME_ASSERT(l_pValue);
    DOME_ASSERT(l_pValue->isF32());
    Int l_Mode = Int(l_pValue->getF32() + 0.5f);

    static const DStringHash k_MDOperator_MDDistoration0("MDDistoration0");
    static const DStringHash k_MDOperator_MDDistoration1("MDDistoration1");
    if (l_Mode == 0)
    {
        const MDOperator* l_pMDDistoration = RCManager::Instance().getMDOperator(k_MDOperator_MDDistoration0);
        l_Result = o_pStack->pushOperatorGpu((const MDOperatorGpu*)l_pMDDistoration);
        DOME_ASSERT(DM_SUCC(l_Result));
    }
    else
    {
        const MDOperator* l_pMDDistoration = RCManager::Instance().getMDOperator(k_MDOperator_MDDistoration1);
        l_Result = o_pStack->pushOperatorGpu((const MDOperatorGpu*)l_pMDDistoration);
        DOME_ASSERT(DM_SUCC(l_Result));
    }

    return cacheResult(o_pStack, i_OutputSelector);
}

void            RCDistorationNode::finishLoad()
{

}


RC_NAMESPACE_END