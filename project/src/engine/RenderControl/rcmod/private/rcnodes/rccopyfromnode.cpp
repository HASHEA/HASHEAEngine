#include "pch.h"
/*
    filename:       rccopyfromnode.cpp
    author:         Ming Dong
    date:           2016-JUN-28
    description:    
*/

#include "../../public/rcmod.h"
#include "rccopyfromnode.h"

RC_NAMESPACE_BEGIN

RCEffectNode*    RCCopyFromNode::Create(RCEffect* i_pEffect)
{
    return DOME_New(RCCopyFromNode)(i_pEffect);
}

DResult          RCCopyFromNode::Destroy(RCEffectNode* i_pEffectNode)
{
    DOME_Del(i_pEffectNode);
    return R_SUCCESS;
}

RCCopyFromNode::RCCopyFromNode(RCEffect* i_pEffect)
: RCEffectNode(i_pEffect)
, m_UVPos(DM_NULL)
, m_UVSize(DM_NULL)
{

}

RCCopyFromNode::~RCCopyFromNode()
{

}

DResult         RCCopyFromNode::buildMDEffect(MDEffect* o_pStack, Int i_OutputSelector)
{
    DResult l_Result;

    DOME_ASSERT(i_OutputSelector == 0);

    executePushInput(o_pStack, 0);

    DSimpleTypedValue* l_pValue = getParam(0);
    DOME_ASSERT(l_pValue && l_pValue->isDVector2f());
    DOME_ASSERT(getParamName(0) == "UVPos");
    m_UVPos.set(l_pValue);
    o_pStack->pushOperand(&m_UVPos);

    l_pValue = getParam(1);
    DOME_ASSERT(l_pValue && l_pValue->isDVector2f());
    DOME_ASSERT(getParamName(1) == "UVSize");
    m_UVSize.set(l_pValue);
    o_pStack->pushOperand(&m_UVSize);

    static const DStringHash k_MDOperator_MDCopyRect("MDCopyRect");
    const MDOperator* l_pMDCopyRect = RCManager::Instance().getMDOperator(k_MDOperator_MDCopyRect);
    l_Result = o_pStack->pushOperatorGpu((const MDOperatorGpu*)l_pMDCopyRect);
    DOME_ASSERT(DM_SUCC(l_Result));

    return cacheResult(o_pStack, 0);
}

void            RCCopyFromNode::finishLoad()
{

}


RC_NAMESPACE_END