#include "pch.h"
#include "../../public/rcmod.h"
#include "rceyeadaptationnode.h"

RC_NAMESPACE_BEGIN

RCEffectNode*    RCEyeAdaptationNode::Create(RCEffect* i_pEffect)
{
    return DOME_New(RCEyeAdaptationNode)(i_pEffect);
}

DResult          RCEyeAdaptationNode::Destroy(RCEffectNode* i_pEffectNode)
{
    DOME_Del(i_pEffectNode);
    return R_SUCCESS;
}

RCEyeAdaptationNode::RCEyeAdaptationNode(RCEffect* i_pEffect)
: RCEffectNode(i_pEffect)
, m_Frame0(RCGlobal::k_SimpleTypeID_OSTexture2D)
, m_Frame1(RCGlobal::k_SimpleTypeID_OSTexture2D)
, m_bTextureCreated(false)
{

}

RCEyeAdaptationNode::~RCEyeAdaptationNode()
{
	RCRenderer* l_pRenderer = getRCEffect()->getEffectManager()->getRenderer();
	OSTexture2D l_tex = *m_Frame0.getTexturePtr();
	if (l_tex.isValid())
	{
		l_pRenderer->destroyTexture2D(l_tex);
	}

	l_tex = *m_Frame1.getTexturePtr();
	if (l_tex.isValid())
	{
		l_pRenderer->destroyTexture2D(l_tex);
	}
}

DResult         RCEyeAdaptationNode::buildMDEffect(MDEffect* o_pStack, Int i_OutputSelector)
{
    DResult l_Result;

    DOME_ASSERT(i_OutputSelector == 0);

    executePushInput(o_pStack, 0);

	const MDOperand* l_pTopOperand = o_pStack->getTopOperand();
	DOME_ASSERT(l_pTopOperand);
	DOME_ASSERT(l_pTopOperand->isTexture());

	if (!m_bTextureCreated)
	{
		RCRenderer* l_pRenderer = getRCEffect()->getEffectManager()->getRenderer();
		DVector2i l_ColorInputSize;
		RCGPUDATAFORMAT l_Format;
		const MDOperand* l_pColorInput = l_pTopOperand;
		l_pColorInput->getTextureSize(l_ColorInputSize);
		l_pColorInput->getTextureFormat(l_Format);

		l_Result = l_pRenderer->createTexture2D(*m_Frame0.getDataPtr()->getValuePtr<OSTexture2D>(), l_ColorInputSize.x, l_ColorInputSize.y, 1, l_Format, RBU_DEFAULT, DM_FALSE, DM_NULL);
		DOME_ASSERT(DM_SUCC(l_Result));

		l_Result = l_pRenderer->createTexture2D(*m_Frame1.getDataPtr()->getValuePtr<OSTexture2D>(), l_ColorInputSize.x, l_ColorInputSize.y, 1, l_Format, RBU_DEFAULT, DM_FALSE, DM_NULL);
		DOME_ASSERT(DM_SUCC(l_Result));

		m_bTextureCreated = true;
	}

	o_pStack->pushOperand(&m_Frame0);
	o_pStack->pushOperand(&m_Frame1);

	executePushInput(o_pStack, 1);
	executePushInput(o_pStack, 2);
	executePushInput(o_pStack, 3);
	executePushInput(o_pStack, 4);

	// SGet and Set Operation
	static const DStringHash k_MDOperator_MDOperatorName("MDEyeAdaptation");
	const MDOperator* l_pMDOperator = RCManager::Instance().getMDOperator(k_MDOperator_MDOperatorName);
	l_Result = o_pStack->pushOperatorCpu((const MDOperatorCpu*)l_pMDOperator);
	DOME_ASSERT(DM_SUCC(l_Result));
	
    return cacheResult(o_pStack, 0);
}

void            RCEyeAdaptationNode::finishLoad()
{

}


RC_NAMESPACE_END