#include "pch.h"
/*
    filename:       rccolorinputnode.cpp
    author:         Ming Dong
    date:           2016-MAY-24
    description:    
*/
#include "../../public/rcmod.h"
#include "rccolorinputnode.h"

RC_NAMESPACE_BEGIN

RCEffectNode*    RCColorInputNode::Create(RCEffect* i_pEffect)
{
    return DOME_New(RCColorInputNode)(i_pEffect);
}

DResult          RCColorInputNode::Destroy(RCEffectNode* i_pEffectNode)
{
    DOME_Del(i_pEffectNode);
    return R_SUCCESS;
}

RCColorInputNode::RCColorInputNode(RCEffect* i_pEffect)
: RCEffectNode(i_pEffect)
, m_ColorInputTexture(DM_NULL)
{

}

RCColorInputNode::~RCColorInputNode()
{

}

DResult         RCColorInputNode::buildMDEffect(MDEffect* o_pStack, Int i_OutputSelector)
{
    o_pStack->pushOperand(&m_ColorInputTexture);
    return R_SUCCESS;
}

void            RCColorInputNode::finishLoad()
{
    const static DStringHash k_ColorInputTex(k_KEY_ColorInput);
    DSimpleTypedValue* l_pValue = getRCEffect()->getEffectManager()->getParamSys().getParameter(k_ColorInputTex);
    m_ColorInputTexture.set(l_pValue);
}


RC_NAMESPACE_END