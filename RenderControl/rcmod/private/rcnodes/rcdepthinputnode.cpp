#include "pch.h"
/*
    filename:       rcdepthinputnode.cpp
    author:         Ming Dong
    date:           2016-Aug-17
    description:    
*/
#include "../../public/rcmod.h"
#include "rcdepthinputnode.h"

RC_NAMESPACE_BEGIN

RCEffectNode*    RCDepthInputNode::Create(RCEffect* i_pEffect)
{
    return DOME_New(RCDepthInputNode)(i_pEffect);
}

DResult          RCDepthInputNode::Destroy(RCEffectNode* i_pEffectNode)
{
    DOME_Del(i_pEffectNode);
    return R_SUCCESS;
}

RCDepthInputNode::RCDepthInputNode(RCEffect* i_pEffect)
: RCEffectNode(i_pEffect)
, m_DepthTexture(DM_NULL)
{

}

RCDepthInputNode::~RCDepthInputNode()
{

}

DResult         RCDepthInputNode::buildMDEffect(MDEffect* o_pStack, Int i_OutputSelector)
{
    o_pStack->pushOperand(&m_DepthTexture);
    return R_SUCCESS;
}

void            RCDepthInputNode::finishLoad()
{
    const static DStringHash k_DepthTex(k_KEY_DepthInput);
    DSimpleTypedValue* l_pValue = getRCEffect()->getEffectManager()->getParamSys().getParameter(k_DepthTex);
    m_DepthTexture.set(l_pValue);
}


RC_NAMESPACE_END