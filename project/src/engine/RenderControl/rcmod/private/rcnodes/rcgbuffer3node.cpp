#include "pch.h"
/*
    filename:       rcgbuffer3node.cpp
    author:         Ming Dong
    date:           2016-Aug-17
    description:    
*/
#include "../../public/rcmod.h"
#include "rcgbuffer3node.h"

RC_NAMESPACE_BEGIN

RCEffectNode*    RCGBuffer3Node::Create(RCEffect* i_pEffect)
{
    return DOME_New(RCGBuffer3Node)(i_pEffect);
}

DResult          RCGBuffer3Node::Destroy(RCEffectNode* i_pEffectNode)
{
    DOME_Del(i_pEffectNode);
    return R_SUCCESS;
}

RCGBuffer3Node::RCGBuffer3Node(RCEffect* i_pEffect)
: RCEffectNode(i_pEffect)
, m_GBuffer3Texture(DM_NULL)
{

}

RCGBuffer3Node::~RCGBuffer3Node()
{

}

DResult         RCGBuffer3Node::buildMDEffect(MDEffect* o_pStack, Int i_OutputSelector)
{
    o_pStack->pushOperand(&m_GBuffer3Texture);
    return R_SUCCESS;
}

void            RCGBuffer3Node::finishLoad()
{
    const static DStringHash k_GBuffer3Tex(k_KEY_GBuffer3);
    DSimpleTypedValue* l_pValue = getRCEffect()->getEffectManager()->getParamSys().getParameter(k_GBuffer3Tex);
    m_GBuffer3Texture.set(l_pValue);
}


RC_NAMESPACE_END