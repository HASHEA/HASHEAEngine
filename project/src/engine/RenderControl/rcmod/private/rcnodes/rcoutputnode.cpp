#include "pch.h"
/*
    filename:       rcoutputnode.cpp
    author:         Ming Dong
    date:           2016-MAY-24
    description:    
*/
#include "rcoutputnode.h"
#include "../../../DevEnv/Include/PerfAnalyzer.h"

RC_NAMESPACE_BEGIN

RCEffectNode*    RCOutputNode::Create(RCEffect* i_pEffect)
{
    return DOME_New(RCOutputNode)(i_pEffect);
}

DResult          RCOutputNode::Destroy(RCEffectNode* i_pEffectNode)
{
    DOME_Del(i_pEffectNode);
    return R_SUCCESS;
}

RCOutputNode::RCOutputNode(RCEffect* i_pEffect)
:RCEffectNode(i_pEffect)
,m_RTTexture(RCGlobal::k_SimpleTypeID_OSTexture2D)
, m_SrcUVPos(RCGlobal::k_SimpleTypeID_DVector2f)
, m_SrcUVSize(RCGlobal::k_SimpleTypeID_DVector2f)
, m_DstUVPos(RCGlobal::k_SimpleTypeID_DVector2f)
, m_DstUVSize(RCGlobal::k_SimpleTypeID_DVector2f)
, m_BlendType(RCGlobal::k_SimpleTypeID_F32)
, m_WriteFrequency(RCGlobal::k_SimpleTypeID_F32)
, m_FilePath(RCGlobal::k_SimpleTypeID_DString)
{

}

RCOutputNode::~RCOutputNode()
{

}

DResult         RCOutputNode::buildMDEffect(MDEffect* o_pStack, Int i_OutputSelector)
{
	PERF_COUNTER_EX(0);
    DResult l_Result;
    l_Result = executePushInput(o_pStack, 0);
    DOME_ASSERT(DM_SUCC(l_Result));

    DSimpleTypedValue* l_pBlendMode = getParam(1);
    DSimpleTypedValue* l_pSrcUVPos = getParam(2);
    DSimpleTypedValue* l_pSrcUVSize = getParam(3);
    DSimpleTypedValue* l_pDstUVPos = getParam(4);
    DSimpleTypedValue* l_pDstUVSize = getParam(5);
	DSimpleTypedValue* l_pBIsWriteToTexture = getParam(6);
	DSimpleTypedValue* l_pWriteFrequency = getParam(7);
	DSimpleTypedValue* l_pStrFilePath = getParam(8);

	bool l_bNeedWriteToTexture = l_pBIsWriteToTexture->getF32() > 0.5f ? true : false;
	if (l_bNeedWriteToTexture)
	{
		static const DStringHash k_MDOperator_MDRenderToTexture("MDRenderToTexture");
		const MDOperator* l_pMDRenderToTexture = RCManager::Instance().getMDOperator(k_MDOperator_MDRenderToTexture);

		RCEffectManager* l_pEffectMgr = o_pStack->getRCEffect()->getEffectManager();

		m_WriteFrequency.getDataPtr()->setF32(l_pWriteFrequency->getF32());
		m_FilePath.getDataPtr()->setDString(l_pStrFilePath->getDString());

		o_pStack->pushOperand(&m_WriteFrequency);
		o_pStack->pushOperand(&m_FilePath);
		o_pStack->pushOperatorCpu((const MDOperatorCpu*)l_pMDRenderToTexture);
	}

    if (l_pBlendMode && l_pBlendMode->getF32() > 1.5f && l_pBlendMode->getF32() < 2.5f)
    {
        static const DStringHash k_MDOperator_MDOutput_Customize("MDOutput_Customize");
        const MDOperator* l_pMDOutput = RCManager::Instance().getMDOperator(k_MDOperator_MDOutput_Customize);

        RCEffectManager* l_pEffectMgr = o_pStack->getRCEffect()->getEffectManager();
        RCRenderer* l_pRenderer = l_pEffectMgr->getRenderer();

        OSTexture2D l_OutputTex;
        l_Result = l_pEffectMgr->getParamSys().getOSTexture2D("EXTERN::RT::Output", l_OutputTex);

        m_RTTexture.getDataPtr()->set(RCGlobal::k_SimpleTypeID_OSTexture2D, &l_OutputTex);
        o_pStack->pushOperand(&m_RTTexture);

        m_SrcUVPos.getDataPtr()->setDVector2f(l_pSrcUVPos->getDVector2f());
        m_SrcUVSize.getDataPtr()->setDVector2f(l_pSrcUVSize->getDVector2f());
        m_DstUVPos.getDataPtr()->setDVector2f(l_pDstUVPos->getDVector2f());
        m_DstUVSize.getDataPtr()->setDVector2f(l_pDstUVSize->getDVector2f());
        m_BlendType.getDataPtr()->setF32(l_pBlendMode->getF32());

        o_pStack->pushOperand(&m_SrcUVPos);
        o_pStack->pushOperand(&m_SrcUVSize);
        o_pStack->pushOperand(&m_DstUVPos);
        o_pStack->pushOperand(&m_DstUVSize);
        o_pStack->pushOperand(&m_BlendType);

        return o_pStack->pushOperatorCpu((const MDOperatorCpu*)l_pMDOutput);
    }

    if (!l_pBlendMode || !l_pSrcUVPos || !l_pSrcUVSize || !l_pDstUVPos || !l_pDstUVSize ||
        (l_pBlendMode->getF32() < 0.5f))
    {
        if (
            (l_pSrcUVPos && l_pSrcUVPos->getDVector2f() != DVector2f(0.0f, 0.0f)) ||
            (l_pSrcUVSize && l_pSrcUVSize->getDVector2f() != DVector2f(1.0f, 1.0f)) ||
            (l_pDstUVPos && l_pDstUVPos->getDVector2f() != DVector2f(0.0f, 0.0f)) ||
            (l_pDstUVSize && l_pDstUVSize->getDVector2f() != DVector2f(1.0f, 1.0f))
            )
        {
            static const DStringHash k_MDOperator_MDCopyRectTo("MDCopyRectTo");
            const MDOperator* l_pMDOutput = RCManager::Instance().getMDOperator(k_MDOperator_MDCopyRectTo);

            RCEffectManager* l_pEffectMgr = o_pStack->getRCEffect()->getEffectManager();
            RCRenderer* l_pRenderer = l_pEffectMgr->getRenderer();
            OSTexture2D l_OutputTex;
            DVector2i   l_Size;
            l_Result = l_pEffectMgr->getParamSys().getOSTexture2D("EXTERN::RT::Output", l_OutputTex);

            m_RTTexture.getDataPtr()->set(RCGlobal::k_SimpleTypeID_OSTexture2D, &l_OutputTex);
            o_pStack->pushOperand(&m_RTTexture);

            m_SrcUVPos.getDataPtr()->setDVector2f(l_pSrcUVPos->getDVector2f());
            m_SrcUVSize.getDataPtr()->setDVector2f(l_pSrcUVSize->getDVector2f());
            m_DstUVPos.getDataPtr()->setDVector2f(l_pDstUVPos->getDVector2f());
            m_DstUVSize.getDataPtr()->setDVector2f(l_pDstUVSize->getDVector2f());

            o_pStack->pushOperand(&m_SrcUVPos);
            o_pStack->pushOperand(&m_SrcUVSize);
            o_pStack->pushOperand(&m_DstUVPos);
            o_pStack->pushOperand(&m_DstUVSize);

            return o_pStack->pushOperatorCpu((const MDOperatorCpu*)l_pMDOutput);
        }
        else
        {
            static const DStringHash k_MDOperator_MDOutput("MDOutput");
            const MDOperator* l_pMDOutput = RCManager::Instance().getMDOperator(k_MDOperator_MDOutput);
            return o_pStack->pushOperatorGpu((const MDOperatorGpu*)l_pMDOutput);
        }
    }

    static const DStringHash k_MDOperator_MDBlendRectTo("MDBlendRectTo");
    const MDOperator* l_pMDOutput = RCManager::Instance().getMDOperator(k_MDOperator_MDBlendRectTo);

    RCEffectManager* l_pEffectMgr = o_pStack->getRCEffect()->getEffectManager();
    RCRenderer* l_pRenderer = l_pEffectMgr->getRenderer();
    OSTexture2D l_OutputTex;
    DVector2i   l_Size;
    l_Result = l_pEffectMgr->getParamSys().getOSTexture2D("EXTERN::RT::Output", l_OutputTex);

    m_RTTexture.getDataPtr()->set(RCGlobal::k_SimpleTypeID_OSTexture2D, &l_OutputTex);
    o_pStack->pushOperand(&m_RTTexture);

    m_SrcUVPos.getDataPtr()->setDVector2f(l_pSrcUVPos->getDVector2f());
    m_SrcUVSize.getDataPtr()->setDVector2f(l_pSrcUVSize->getDVector2f());
    m_DstUVPos.getDataPtr()->setDVector2f(l_pDstUVPos->getDVector2f());
    m_DstUVSize.getDataPtr()->setDVector2f(l_pDstUVSize->getDVector2f());

    o_pStack->pushOperand(&m_SrcUVPos);
    o_pStack->pushOperand(&m_SrcUVSize);
    o_pStack->pushOperand(&m_DstUVPos);
    o_pStack->pushOperand(&m_DstUVSize);

    return o_pStack->pushOperatorCpu((const MDOperatorCpu*)l_pMDOutput);
}

void            RCOutputNode::finishLoad()
{

}


RC_NAMESPACE_END