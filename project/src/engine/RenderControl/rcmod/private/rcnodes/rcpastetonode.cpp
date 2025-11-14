#include "pch.h"
/*
    filename:       rcpastetonode.cpp
    author:         Ming Dong
    date:           2016-JUN-28
    description:    
*/

#include "../../public/rcmod.h"
#include "rcpastetonode.h"

RC_NAMESPACE_BEGIN

RCEffectNode*    RCPasteToNode::Create(RCEffect* i_pEffect)
{
    return DOME_New(RCPasteToNode)(i_pEffect);
}

DResult          RCPasteToNode::Destroy(RCEffectNode* i_pEffectNode)
{
    DOME_Del(i_pEffectNode);
    return R_SUCCESS;
}

RCPasteToNode::RCPasteToNode(RCEffect* i_pEffect)
: RCEffectNode(i_pEffect)
, m_SrcUVPos(DM_NULL)
, m_SrcUVSize(DM_NULL)
, m_TrgUVPos(DM_NULL)
, m_TrgUVSize(DM_NULL)
{

}

RCPasteToNode::~RCPasteToNode()
{

}

DResult         RCPasteToNode::buildMDEffect(MDEffect* o_pStack, Int i_OutputSelector)
{
    DResult l_Result;

    DOME_ASSERT(i_OutputSelector == 0);

    executePushInput(o_pStack, 0);
    executePushInput(o_pStack, 1);

    DSimpleTypedValue* l_pValue = getParam(0);
    DOME_ASSERT(l_pValue && l_pValue->isDVector2f());
    DOME_ASSERT(getParamName(0) == "SrcUVPos");
    m_SrcUVPos.set(l_pValue);
    o_pStack->pushOperand(&m_SrcUVPos);

    l_pValue = getParam(1);
    DOME_ASSERT(l_pValue && l_pValue->isDVector2f());
    DOME_ASSERT(getParamName(1) == "SrcUVSize");
    m_SrcUVSize.set(l_pValue);
    o_pStack->pushOperand(&m_SrcUVSize);

    l_pValue = getParam(2);
    DOME_ASSERT(l_pValue && l_pValue->isDVector2f());
    DOME_ASSERT(getParamName(2) == "TargetUVPos");
    m_TrgUVPos.set(l_pValue);
    o_pStack->pushOperand(&m_TrgUVPos);

    l_pValue = getParam(3);
    DOME_ASSERT(l_pValue && l_pValue->isDVector2f());
    DOME_ASSERT(getParamName(3) == "TargetUVSize");
    m_TrgUVSize.set(l_pValue);
    o_pStack->pushOperand(&m_TrgUVSize);

    static const DStringHash k_MDOperator_MDCopyRectTo("MDCopyRectTo");
    const MDOperator* l_pMDCopyRectTo = RCManager::Instance().getMDOperator(k_MDOperator_MDCopyRectTo);
    l_Result = o_pStack->pushOperatorCpu((const MDOperatorCpu*)l_pMDCopyRectTo);
    DOME_ASSERT(DM_SUCC(l_Result));

    return cacheResult(o_pStack, 0);
}

void            RCPasteToNode::finishLoad()
{

}


RC_NAMESPACE_END