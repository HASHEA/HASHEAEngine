#include "pch.h"
/*
    filename:       rcaddconstnode.cpp
    author:         Ming Dong
    date:           2016-JUN-27
    description:    
*/
#include "../../public/rcmod.h"
#include "rcdeferedframenode.h"

RC_NAMESPACE_BEGIN

RCEffectNode*    RCDeferedFrameNode::Create(RCEffect* i_pEffect)
{
    return DOME_New(RCDeferedFrameNode)(i_pEffect);
}

DResult          RCDeferedFrameNode::Destroy(RCEffectNode* i_pEffectNode)
{
    DOME_Del(i_pEffectNode);
    return R_SUCCESS;
}

RCDeferedFrameNode::RCDeferedFrameNode(RCEffect* i_pEffect)
: RCEffectNode(i_pEffect)
, m_ValueTypeID(RCGlobal::k_SimpleTypeID_Unknown)
, m_CurrFrameOperand(DM_NULL)
, m_LastFrameOperand(DM_NULL)
{

}

RCDeferedFrameNode::~RCDeferedFrameNode()
{
    if (m_ValueTypeID == RCGlobal::k_SimpleTypeID_OSTexture2D)
    {
        RCRenderer* l_pRenderer = getRCEffect()->getEffectManager()->getRenderer();
        for (Int i = 0; i < m_ValuePool.size(); ++i)
        {
            l_pRenderer->destroyTexture2D(m_ValuePool[i].getValue<OSTexture2D>());
        }
    }
}

DResult         RCDeferedFrameNode::buildMDEffect(MDEffect* o_pStack, Int i_OutputSelector)
{
    DOME_ASSERT(i_OutputSelector == 0);

    DResult l_Result;
    DSimpleTypedValue* l_pDelayFrameValue = getParam(0);
    DOME_ASSERT(l_pDelayFrameValue);
    DOME_ASSERT(l_pDelayFrameValue->isF32());
    F32 l_fDelayFrame = l_pDelayFrameValue->getF32();
    Int l_DelayFrame = Int(l_fDelayFrame + 0.5f);
    if(l_DelayFrame < 0)
        l_DelayFrame = 0;
    if(l_DelayFrame > 10)
        l_DelayFrame = 10;

    executePushInput(o_pStack, 0);

    const MDOperand* l_pTopOperand = o_pStack->getTopOperand();
    DOME_ASSERT(l_pTopOperand);

    if (m_ValueTypeID == RCGlobal::k_SimpleTypeID_Unknown)
    {
        if(l_pTopOperand->isTexture())
            m_ValueTypeID = RCGlobal::k_SimpleTypeID_OSTexture2D;
        else if(l_pTopOperand->isMatrix4x4())
            m_ValueTypeID = RCGlobal::k_SimpleTypeID_DMatrix4x4f;
        else
        {
            DOME_ASSERT(0);
        }
    }
    DOME_ASSERT(m_ValueTypeID == l_pTopOperand->getDataType(0));

    if (l_pTopOperand->isTexture())
    {
        RCRenderer* l_pRenderer = getRCEffect()->getEffectManager()->getRenderer();
        DVector2i l_ColorInputSize;
        RCGPUDATAFORMAT l_Format;
        const MDOperand* l_pColorInput = l_pTopOperand;
        l_pColorInput->getTextureSize(l_ColorInputSize);
        l_pColorInput->getTextureFormat(l_Format);

        OSTexture2D l_CurFrameTex;
        l_Result = l_pRenderer->createTexture2D(l_CurFrameTex, l_ColorInputSize.x, l_ColorInputSize.y, 1, l_Format, RBU_DEFAULT, DM_FALSE, DM_NULL);
        DOME_ASSERT(DM_SUCC(l_Result));

        DSimpleTypedValue l_CurFrameTexValue;
        l_CurFrameTexValue.set(RCGlobal::k_SimpleTypeID_OSTexture2D, &l_CurFrameTex);

        m_ValuePool.push_back(l_CurFrameTexValue);

        // remove unused frames
        Int l_NumFrameRemove = m_ValuePool.size() - (l_DelayFrame + 1);
        for (Int i = 0; i < l_NumFrameRemove; ++i)
        {
            l_pRenderer->destroyTexture2D(m_ValuePool[0].getValue<OSTexture2D>());
            m_ValuePool.remove(0);
        }
        DOME_ASSERT(m_ValuePool.size() > 0);


        m_CurrFrameOperand.set(&m_ValuePool[m_ValuePool.size() - 1]);
        o_pStack->pushOperand(&m_CurrFrameOperand);

        static const DStringHash k_MDOperator_MDGpuCopyTo("MDGpuCopyTo");
        const MDOperator* l_pMDGpuCopyTo = RCManager::Instance().getMDOperator(k_MDOperator_MDGpuCopyTo);
        l_Result = o_pStack->pushOperatorCpu((const MDOperatorCpu*)l_pMDGpuCopyTo);
        DOME_ASSERT(DM_SUCC(l_Result));

        DOME_ASSERT(m_ValuePool.size() > 0);

        m_LastFrameOperand.set(&m_ValuePool[0]);
        o_pStack->pushOperand(&m_LastFrameOperand);

        static const DStringHash k_MDOperator_MDLogicDepend("MDLogicDepend");
        const MDOperator* l_pMDLogicDepend = RCManager::Instance().getMDOperator(k_MDOperator_MDLogicDepend);
        l_Result = o_pStack->pushOperatorCpu((const MDOperatorCpu*)l_pMDLogicDepend);
        DOME_ASSERT(DM_SUCC(l_Result));
    }
    else
    {
        DSimpleTypedValue l_CurFrameValue;
        l_CurFrameValue.initType(m_ValueTypeID);

        m_ValuePool.push_back(l_CurFrameValue);

        // remove unused frames
        Int l_NumFrameRemove = m_ValuePool.size() - (l_DelayFrame + 1);
        for (Int i = 0; i < l_NumFrameRemove; ++i)
        {
            m_ValuePool.remove(0);
        }
        DOME_ASSERT(m_ValuePool.size() > 0);


        m_CurrFrameOperand.set(&m_ValuePool[m_ValuePool.size() - 1]);
        o_pStack->pushOperand(&m_CurrFrameOperand);

        static const DStringHash k_MDOperator_MDCopyValueTo("MDCopyValueTo");
        const MDOperator* l_pMDCopyValueTo = RCManager::Instance().getMDOperator(k_MDOperator_MDCopyValueTo);
        l_Result = o_pStack->pushOperatorCpu((const MDOperatorCpu*)l_pMDCopyValueTo);
        DOME_ASSERT(DM_SUCC(l_Result));

        DOME_ASSERT(m_ValuePool.size() > 0);

        m_LastFrameOperand.set(&m_ValuePool[0]);
        o_pStack->pushOperand(&m_LastFrameOperand);

        static const DStringHash k_MDOperator_MDLogicDepend("MDLogicDepend");
        const MDOperator* l_pMDLogicDepend = RCManager::Instance().getMDOperator(k_MDOperator_MDLogicDepend);
        l_Result = o_pStack->pushOperatorCpu((const MDOperatorCpu*)l_pMDLogicDepend);
        DOME_ASSERT(DM_SUCC(l_Result));
    }

    return cacheResult(o_pStack, i_OutputSelector);
}

void            RCDeferedFrameNode::finishLoad()
{

}


RC_NAMESPACE_END