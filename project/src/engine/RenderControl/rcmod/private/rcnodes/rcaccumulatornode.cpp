#include "pch.h"
/*
    filename:       RCAccumulatorNode.cpp
    author:         Ming Dong
    date:           2016-JUN-28
    description:    
*/

#include "../../public/rcmod.h"
#include "rcaccumulatornode.h"

RC_NAMESPACE_BEGIN

RCEffectNode*    RCAccumulatorNode::Create(RCEffect* i_pEffect)
{
    return DOME_New(RCAccumulatorNode)(i_pEffect);
}

DResult          RCAccumulatorNode::Destroy(RCEffectNode* i_pEffectNode)
{
    DOME_Del(i_pEffectNode);
    return R_SUCCESS;
}

RCAccumulatorNode::RCAccumulatorNode(RCEffect* i_pEffect)
: RCEffectNode(i_pEffect)
, m_Frame0(RCGlobal::k_SimpleTypeID_OSTexture2D)
, m_Frame1(RCGlobal::k_SimpleTypeID_OSTexture2D)
, m_bTextureCreated(false)
, m_ConstValue(RCGlobal::k_SimpleTypeID_DVector2f)
{

}

RCAccumulatorNode::~RCAccumulatorNode()
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

DResult         RCAccumulatorNode::buildMDEffect(MDEffect* o_pStack, Int i_OutputSelector)
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

	DSimpleTypedValue* l_pAccumTime = getParam(0);
	DSimpleTypedValue* l_pAccumExp = getParam(1);
	

	DVector2f l_Vec2;
	l_Vec2.x = l_pAccumTime->getF32();
	l_Vec2.y = l_pAccumExp->getF32();

	m_ConstValue.getDataPtr()->setDVector2f(l_Vec2);
	o_pStack->pushOperand(&m_ConstValue);

	// SGet and Set Operation
	static const DStringHash k_MDOperator_MDOperatorName("MDAccumulator");
	const MDOperator* l_pMDOperator = RCManager::Instance().getMDOperator(k_MDOperator_MDOperatorName);
	l_Result = o_pStack->pushOperatorCpu((const MDOperatorCpu*)l_pMDOperator);
	DOME_ASSERT(DM_SUCC(l_Result));
	
    return cacheResult(o_pStack, 0);
}

void            RCAccumulatorNode::finishLoad()
{

}


RC_NAMESPACE_END