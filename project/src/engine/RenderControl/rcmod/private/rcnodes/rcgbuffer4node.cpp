#include "pch.h"
/*
    filename:       rcgbuffer4node.cpp
    author:         Ming Dong
    date:           2016-Aug-17
    description:    
*/
#include "../../public/rcmod.h"
#include "rcgbuffer4node.h"

RC_NAMESPACE_BEGIN

RCEffectNode*    RCGBuffer4Node::Create(RCEffect* i_pEffect)
{
    return DOME_New(RCGBuffer4Node)(i_pEffect);
}

DResult          RCGBuffer4Node::Destroy(RCEffectNode* i_pEffectNode)
{
    DOME_Del(i_pEffectNode);
    return R_SUCCESS;
}

RCGBuffer4Node::RCGBuffer4Node(RCEffect* i_pEffect)
: RCEffectNode(i_pEffect)
, m_GBuffer4Texture(DM_NULL)
{

}

RCGBuffer4Node::~RCGBuffer4Node()
{

}

DResult         RCGBuffer4Node::buildMDEffect(MDEffect* o_pStack, Int i_OutputSelector)
{
    o_pStack->pushOperand(&m_GBuffer4Texture);
    return R_SUCCESS;
}

void            RCGBuffer4Node::finishLoad()
{
    const static DStringHash k_GBuffer4Tex(k_KEY_GBuffer4);
    DSimpleTypedValue* l_pValue = getRCEffect()->getEffectManager()->getParamSys().getParameter(k_GBuffer4Tex);
    m_GBuffer4Texture.set(l_pValue);
}


RC_NAMESPACE_END