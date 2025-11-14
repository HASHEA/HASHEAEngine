#include "pch.h"
/*
    filename:       rcgbuffer1node.cpp
    author:         Ming Dong
    date:           2016-Aug-17
    description:    
*/
#include "../../public/rcmod.h"
#include "rcgbuffer1node.h"

RC_NAMESPACE_BEGIN

RCEffectNode*    RCGBuffer1Node::Create(RCEffect* i_pEffect)
{
    return DOME_New(RCGBuffer1Node)(i_pEffect);
}

DResult          RCGBuffer1Node::Destroy(RCEffectNode* i_pEffectNode)
{
    DOME_Del(i_pEffectNode);
    return R_SUCCESS;
}

RCGBuffer1Node::RCGBuffer1Node(RCEffect* i_pEffect)
: RCEffectNode(i_pEffect)
, m_GBuffer1Texture(DM_NULL)
{

}

RCGBuffer1Node::~RCGBuffer1Node()
{

}

DResult         RCGBuffer1Node::buildMDEffect(MDEffect* o_pStack, Int i_OutputSelector)
{
    o_pStack->pushOperand(&m_GBuffer1Texture);
    return R_SUCCESS;
}

void            RCGBuffer1Node::finishLoad()
{
    const static DStringHash k_GBuffer1Tex(k_KEY_GBuffer1);
    DSimpleTypedValue* l_pValue = getRCEffect()->getEffectManager()->getParamSys().getParameter(k_GBuffer1Tex);
    m_GBuffer1Texture.set(l_pValue);
}


RC_NAMESPACE_END