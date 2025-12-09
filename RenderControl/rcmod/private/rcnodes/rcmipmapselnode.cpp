#include "pch.h"
/*
    filename:       rcmipmapselnode.cpp
    author:         Ming Dong
    date:           2016-Aug-11
    description:    
*/
#include "../../public/rcmod.h"
#include "rcmipmapselnode.h"

RC_NAMESPACE_BEGIN

RCEffectNode*    RCMipmapSelNode::Create(RCEffect* i_pEffect)
{
    return DOME_New(RCMipmapSelNode)(i_pEffect);
}

DResult          RCMipmapSelNode::Destroy(RCEffectNode* i_pEffectNode)
{
    DOME_Del(i_pEffectNode);
    return R_SUCCESS;
}

RCMipmapSelNode::RCMipmapSelNode(RCEffect* i_pEffect)
: RCEffectNode(i_pEffect)
, m_MipmapLevel(DM_NULL)
{

}

RCMipmapSelNode::~RCMipmapSelNode()
{

}

DResult         RCMipmapSelNode::buildMDEffect(MDEffect* o_pStack, Int i_OutputSelector)
{
    DOME_ASSERT(i_OutputSelector == 0);

    DResult l_Result;
    executePushInput(o_pStack, 0);

    DSimpleTypedValue* l_pValue = getParam(0);

    m_MipmapLevel.set(l_pValue);
    o_pStack->pushOperand(&m_MipmapLevel);

    static const DStringHash k_MDOperator_MDMipmapSel("MDMipmapSel");
    const MDOperator* l_pMDMipmapSel = RCManager::Instance().getMDOperator(k_MDOperator_MDMipmapSel);
    l_Result = o_pStack->pushOperatorCpu((const MDOperatorCpu*)l_pMDMipmapSel);
    DOME_ASSERT(DM_SUCC(l_Result));

    return cacheResult(o_pStack, i_OutputSelector);
}

void            RCMipmapSelNode::finishLoad()
{

}


RC_NAMESPACE_END