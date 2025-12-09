#include "pch.h"
/*
    filename:       rcgbuffer0node.cpp
    author:         Ming Dong
    date:           2016-Aug-17
    description:    
*/
#include "../../public/rcmod.h"
#include "rcgbuffer0node.h"

RC_NAMESPACE_BEGIN

RCEffectNode*    RCGBuffer0Node::Create(RCEffect* i_pEffect)
{
    return DOME_New(RCGBuffer0Node)(i_pEffect);
}

DResult          RCGBuffer0Node::Destroy(RCEffectNode* i_pEffectNode)
{
    DOME_Del(i_pEffectNode);
    return R_SUCCESS;
}

RCGBuffer0Node::RCGBuffer0Node(RCEffect* i_pEffect)
: RCEffectNode(i_pEffect)
, m_GBuffer0Texture(DM_NULL)
{

}

RCGBuffer0Node::~RCGBuffer0Node()
{

}

DResult         RCGBuffer0Node::buildMDEffect(MDEffect* o_pStack, Int i_OutputSelector)
{
    o_pStack->pushOperand(&m_GBuffer0Texture);
    return R_SUCCESS;
}

void            RCGBuffer0Node::finishLoad()
{
    const static DStringHash k_GBuffer0Tex(k_KEY_GBuffer0);
    DSimpleTypedValue* l_pValue = getRCEffect()->getEffectManager()->getParamSys().getParameter(k_GBuffer0Tex);
    m_GBuffer0Texture.set(l_pValue);
}


RC_NAMESPACE_END