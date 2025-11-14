#include "pch.h"
#include "../../public/rcmod.h"
#include "rcdownscalenode.h"

RC_NAMESPACE_BEGIN

RCEffectNode*    RCDownScaleNode::Create(RCEffect* i_pEffect)
{
    return DOME_New(RCDownScaleNode)(i_pEffect);
}

DResult          RCDownScaleNode::Destroy(RCEffectNode* i_pEffectNode)
{
    DOME_Del(i_pEffectNode);
    return R_SUCCESS;
}

RCDownScaleNode::RCDownScaleNode(RCEffect* i_pEffect)
: RCEffectNode(i_pEffect)
{

}

RCDownScaleNode::~RCDownScaleNode()
{

}

DResult         RCDownScaleNode::buildMDEffect(MDEffect* o_pStack, Int i_OutputSelector)
{
    DOME_ASSERT(i_OutputSelector == 0);

    DResult l_Result;
    executePushInput(o_pStack, 0);

    static const DStringHash k_MDOperator_MDDownScale2x2("MDDownScale2x2");
    static const DStringHash k_MDOperator_MDDownScale3x3("MDDownScale3x3");
    static const DStringHash k_MDOperator_MDDownScale4x4("MDDownScale4x4");

    DSimpleTypedValue* l_pDSModeValue = getParam(0);
    DOME_ASSERT(l_pDSModeValue);
    DOME_ASSERT(l_pDSModeValue->getTypeID() == RCGlobal::k_SimpleTypeID_F32);
    F32 l_DSMode = l_pDSModeValue->getF32();
    if (l_DSMode < 0.5f)
    {
    }
    else if (l_DSMode < 1.5f)
    {
        const MDOperator* l_pMDDownScale2x2 = RCManager::Instance().getMDOperator(k_MDOperator_MDDownScale2x2);
        l_Result = o_pStack->pushOperatorGpu((const MDOperatorGpu*)l_pMDDownScale2x2);
        DOME_ASSERT(DM_SUCC(l_Result));
    }
    else if (l_DSMode < 2.5f)
    {
        const MDOperator* l_pMDDownScale3x3 = RCManager::Instance().getMDOperator(k_MDOperator_MDDownScale3x3);
        l_Result = o_pStack->pushOperatorGpu((const MDOperatorGpu*)l_pMDDownScale3x3);
        DOME_ASSERT(DM_SUCC(l_Result));
    }
    else
    {
        const MDOperator* l_pMDDownScale4x4 = RCManager::Instance().getMDOperator(k_MDOperator_MDDownScale4x4);
        l_Result = o_pStack->pushOperatorGpu((const MDOperatorGpu*)l_pMDDownScale4x4);
        DOME_ASSERT(DM_SUCC(l_Result));
    }

    return cacheResult(o_pStack, i_OutputSelector);
}

void            RCDownScaleNode::finishLoad()
{
}


RC_NAMESPACE_END