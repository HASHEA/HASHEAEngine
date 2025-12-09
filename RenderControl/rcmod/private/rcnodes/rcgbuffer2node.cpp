#include "pch.h"
/*
    filename:       rcgbuffer2node.cpp
    author:         Ming Dong
    date:           2016-Aug-17
    description:    
*/
#include "../../public/rcmod.h"
#include "rcgbuffer2node.h"

RC_NAMESPACE_BEGIN

RCEffectNode*    RCGBuffer2Node::Create(RCEffect* i_pEffect)
{
    return DOME_New(RCGBuffer2Node)(i_pEffect);
}

DResult          RCGBuffer2Node::Destroy(RCEffectNode* i_pEffectNode)
{
    DOME_Del(i_pEffectNode);
    return R_SUCCESS;
}

RCGBuffer2Node::RCGBuffer2Node(RCEffect* i_pEffect)
: RCEffectNode(i_pEffect)
, m_GBuffer2Texture(DM_NULL)
{

}

RCGBuffer2Node::~RCGBuffer2Node()
{

}

DResult         RCGBuffer2Node::buildMDEffect(MDEffect* o_pStack, Int i_OutputSelector)
{
    o_pStack->pushOperand(&m_GBuffer2Texture);
    return R_SUCCESS;
}

void            RCGBuffer2Node::finishLoad()
{
    const static DStringHash k_GBuffer2Tex(k_KEY_GBuffer2);
    DSimpleTypedValue* l_pValue = getRCEffect()->getEffectManager()->getParamSys().getParameter(k_GBuffer2Tex);
    m_GBuffer2Texture.set(l_pValue);
}


RC_NAMESPACE_END