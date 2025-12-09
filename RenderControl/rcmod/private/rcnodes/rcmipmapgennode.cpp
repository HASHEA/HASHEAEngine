#include "pch.h"
/*
    filename:       rcmipmapgennode.cpp
    author:         Ming Dong
    date:           2016-Aug-11
    description:    
*/
#include "../../public/rcmod.h"
#include "rcmipmapgennode.h"

RC_NAMESPACE_BEGIN

RCEffectNode*    RCMipmapGenNode::Create(RCEffect* i_pEffect)
{
    return DOME_New(RCMipmapGenNode)(i_pEffect);
}

DResult          RCMipmapGenNode::Destroy(RCEffectNode* i_pEffectNode)
{
    DOME_Del(i_pEffectNode);
    return R_SUCCESS;
}

RCMipmapGenNode::RCMipmapGenNode(RCEffect* i_pEffect)
: RCEffectNode(i_pEffect)
{

}

RCMipmapGenNode::~RCMipmapGenNode()
{

}

DResult         RCMipmapGenNode::buildMDEffect(MDEffect* o_pStack, Int i_OutputSelector)
{
    DOME_ASSERT(i_OutputSelector == 0);

    DResult l_Result;
    executePushInput(o_pStack, 0);

    static const DStringHash k_MDOperator_MDMipmapGen("MDMipmapGen");
    const MDOperator* l_pMDMipmapGen = RCManager::Instance().getMDOperator(k_MDOperator_MDMipmapGen);
    l_Result = o_pStack->pushOperatorCpu((const MDOperatorCpu*)l_pMDMipmapGen);
    DOME_ASSERT(DM_SUCC(l_Result));

    return cacheResult(o_pStack, i_OutputSelector);
}

void            RCMipmapGenNode::finishLoad()
{

}


RC_NAMESPACE_END