#include "pch.h"
#include "../../public/rcmod.h"
#include "rcChannelBooleannode.h"
#include "domecore\public\math\matrix4x4.h"

RC_NAMESPACE_BEGIN

RCEffectNode*    RCChannelBooleanNode::Create(RCEffect* i_pEffect)
{
    return DOME_New(RCChannelBooleanNode)(i_pEffect);
}

DResult          RCChannelBooleanNode::Destroy(RCEffectNode* i_pEffectNode)
{
    DOME_Del(i_pEffectNode);
    return R_SUCCESS;
}

RCChannelBooleanNode::RCChannelBooleanNode(RCEffect* i_pEffect)
: RCEffectNode(i_pEffect)
, m_ConstValue(RCGlobal::k_SimpleTypeID_DMatrix4x4f)
, m_ConstValue1(RCGlobal::k_SimpleTypeID_DMatrix4x4f)
{

}

RCChannelBooleanNode::~RCChannelBooleanNode()
{

}

// Main Work is Done Here.
DResult         RCChannelBooleanNode::buildMDEffect(MDEffect* o_pStack, Int i_OutputSelector)
{
    DOME_ASSERT(i_OutputSelector == 0);

	// Set Input
    DResult l_Result;

	// Get and Set Value
    DSimpleTypedValue* l_pValue  = getParam(0);
	DSimpleTypedValue* l_pValue1 = getParam(1);
	DSimpleTypedValue* l_pValue2 = getParam(2);
	DSimpleTypedValue* l_pValue3 = getParam(3);
	
	DSimpleTypedValue* l_pValueArray[4] =
	{
		l_pValue,
		l_pValue1,
		l_pValue2,
		l_pValue3
	};
	
	DMatrix4x4f matBackTransform, matFrontTransform;
	for( int i = 0; i < 4; ++i )
	{
		matBackTransform.M(0 , i) = int(l_pValueArray[i]->getValue<float>()) == 0;
		matBackTransform.M(1 , i) = int(l_pValueArray[i]->getValue<float>()) == 1;
		matBackTransform.M(2 , i) = int(l_pValueArray[i]->getValue<float>()) == 2;
		matBackTransform.M(3 , i) = int(l_pValueArray[i]->getValue<float>()) == 3;
		
		matFrontTransform.M(0 , i) = int(l_pValueArray[i]->getValue<float>()) == 4;
        matFrontTransform.M(1 , i) = int(l_pValueArray[i]->getValue<float>()) == 5;
        matFrontTransform.M(2 , i) = int(l_pValueArray[i]->getValue<float>()) == 6;
        matFrontTransform.M(3 , i) = int(l_pValueArray[i]->getValue<float>()) == 7;
	}
	
	/// 1. Select Background Color
	executePushInput(o_pStack, 0);
    m_ConstValue.getDataPtr()->set(RCGlobal::k_SimpleTypeID_DMatrix4x4f, &matBackTransform);
    o_pStack->pushOperand(&m_ConstValue);

	// SGet and Set Operation
    static const DStringHash k_MDOperator_MDOperatorName("MDVec4MulMatrix");
    const MDOperator* l_pMDOperator = RCManager::Instance().getMDOperator(k_MDOperator_MDOperatorName);
    l_Result = o_pStack->pushOperatorGpu((const MDOperatorGpu*)l_pMDOperator);
    DOME_ASSERT(DM_SUCC(l_Result));
	if(isInputConnected(1))
	{
		/// 2. Select Front Color
		executePushInput(o_pStack, 1);
		m_ConstValue1.getDataPtr()->set(RCGlobal::k_SimpleTypeID_DMatrix4x4f, &matFrontTransform);
		o_pStack->pushOperand(&m_ConstValue1);
		// SGet and Set Operation
		l_Result = o_pStack->pushOperatorGpu((const MDOperatorGpu*)l_pMDOperator);
		DOME_ASSERT(DM_SUCC(l_Result));

		/// 3. Mix the result
		static const DStringHash k_MDOperator_MDOperatorName1("MDAdd");
		const MDOperator* l_pMDOperator1 = RCManager::Instance().getMDOperator(k_MDOperator_MDOperatorName1);
		l_Result = o_pStack->pushOperatorGpu((const MDOperatorGpu*)l_pMDOperator1);
		DOME_ASSERT(DM_SUCC(l_Result));
	}
	
	
    return cacheResult(o_pStack, i_OutputSelector);
}

void            RCChannelBooleanNode::finishLoad()
{

}


RC_NAMESPACE_END