#include "pch.h"
/*
    filename:       rcgeneralinputnode.cpp
    author:         Ming Dong
    date:           2016-Aug-15
    description:    
*/

#include "../../public/rcmod.h"
#include "rcgeneralinputnode.h"

RC_NAMESPACE_BEGIN

RCEffectNode*    RCGeneralInputNode::Create(RCEffect* i_pEffect)
{
    return DOME_New(RCGeneralInputNode)(i_pEffect);
}

DResult          RCGeneralInputNode::Destroy(RCEffectNode* i_pEffectNode)
{
    DOME_Del(i_pEffectNode);
    return R_SUCCESS;
}

RCGeneralInputNode::RCGeneralInputNode(RCEffect* i_pEffect)
: RCEffectNode(i_pEffect)
, m_Operand(DM_NULL)
{

}

RCGeneralInputNode::~RCGeneralInputNode()
{

}

DResult         RCGeneralInputNode::buildMDEffect(MDEffect* o_pStack, Int i_OutputSelector)
{
    DOME_ASSERT(i_OutputSelector == 0);

    DSimpleTypedValue* l_InputNameValue = getParam(0);
    DOME_ASSERT(l_InputNameValue);
    DOME_ASSERT(l_InputNameValue->isDString());

    DString l_InputName = l_InputNameValue->getDString();
    DStringHash l_InputNameHash(l_InputName.c_str());
    
    RCEffectManager* l_pEffectMgr = getRCEffect()->getEffectManager();
    DSimpleTypedValue* l_pInput = l_pEffectMgr->getParamSys().getParameter(l_InputNameHash);

    DOME_ASSERT(l_pInput);
    m_Operand.set(l_pInput);

    o_pStack->pushOperand(&m_Operand);

    return R_SUCCESS;
}

void            RCGeneralInputNode::finishLoad()
{

}


RC_NAMESPACE_END