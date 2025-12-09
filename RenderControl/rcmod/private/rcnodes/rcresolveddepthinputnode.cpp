#include "pch.h"
/*
    filename:       rcresolveddepthinputnode.cpp
    author:         Ming Dong
    date:           2017-Jun-29
    description:    
*/
#include "../../public/rcmod.h"
#include "rcresolveddepthinputnode.h"

RC_NAMESPACE_BEGIN

RCEffectNode*    RCResolvedDepthInputNode::Create(RCEffect* i_pEffect)
{
    return DOME_New(RCResolvedDepthInputNode)(i_pEffect);
}

DResult          RCResolvedDepthInputNode::Destroy(RCEffectNode* i_pEffectNode)
{
    DOME_Del(i_pEffectNode);
    return R_SUCCESS;
}

RCResolvedDepthInputNode::RCResolvedDepthInputNode(RCEffect* i_pEffect)
: RCEffectNode(i_pEffect)
, m_DepthTexture(DM_NULL)
{

}

RCResolvedDepthInputNode::~RCResolvedDepthInputNode()
{

}

DResult         RCResolvedDepthInputNode::buildMDEffect(MDEffect* o_pStack, Int i_OutputSelector)
{
    o_pStack->pushOperand(&m_DepthTexture);
    return R_SUCCESS;
}

void            RCResolvedDepthInputNode::finishLoad()
{
    const static DStringHash k_DepthTex(k_KEY_ResolvedDepthInput);
    DSimpleTypedValue* l_pValue = getRCEffect()->getEffectManager()->getParamSys().getParameter(k_DepthTex);
    m_DepthTexture.set(l_pValue);
}


RC_NAMESPACE_END