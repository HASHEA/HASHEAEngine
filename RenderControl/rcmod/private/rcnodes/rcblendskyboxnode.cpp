#include "pch.h"
#include "../../public/rcmod.h"
#include "rcblendskyboxnode.h"

RC_NAMESPACE_BEGIN

RCEffectNode* RCBlendSkyboxNode::Create(RCEffect* i_pEffect)
{
    return DOME_New(RCBlendSkyboxNode)(i_pEffect);
}

DResult          RCBlendSkyboxNode::Destroy(RCEffectNode* i_pEffectNode)
{
    DOME_Del(i_pEffectNode);
    return R_SUCCESS;
}
RCBlendSkyboxNode::RCBlendSkyboxNode(RCEffect* i_pEffect)
: RCEffectNode(i_pEffect)
, PosDirOperand(DM_NULL)
, ScaleAngleOperand(DM_NULL)
, RotAngleOperand(DM_NULL)
, CastColorOperand(DM_NULL)
, TextureDimensionOperand(DM_NULL)
, FrameRateOperand(DM_NULL)
{
	
}

RCBlendSkyboxNode::~RCBlendSkyboxNode()
{
	
}

// Main Work is Done Here.
DResult         RCBlendSkyboxNode::buildMDEffect(MDEffect* o_pStack, Int i_OutputSelector)
{
    DOME_ASSERT(i_OutputSelector == 0);
	DOME_ASSERT(getParamType(0) == RCGlobal::k_SimpleTypeID_DVector3f);
	DOME_ASSERT(getParamType(1) == RCGlobal::k_SimpleTypeID_F32);
	DOME_ASSERT(getParamType(2) == RCGlobal::k_SimpleTypeID_F32);
	DOME_ASSERT(getParamType(3) == RCGlobal::k_SimpleTypeID_DVector4f); 
	DOME_ASSERT(getParamType(4) == RCGlobal::k_SimpleTypeID_DVector2f);
	DOME_ASSERT(getParamType(5) == RCGlobal::k_SimpleTypeID_F32);
	static const DStringHash MDOperatorBlendSkybox("MDBlendSkyboxOptions");
    const MDOperator* l_pMDOperatorBlendSkybox = RCManager::Instance().getMDOperator(MDOperatorBlendSkybox);
	
	
	PosDirOperand.set(getParam(0));
	ScaleAngleOperand.set(getParam(1));
	RotAngleOperand.set(getParam(2));
	CastColorOperand.set(getParam(3));
	TextureDimensionOperand.set(getParam(4));
	FrameRateOperand.set(getParam(5));
	DResult l_Result;
	{
		executePushInput(o_pStack, 0);
		executePushInput(o_pStack, 1);
		executePushInput(o_pStack, 2);
		o_pStack->pushOperand(&PosDirOperand);
		o_pStack->pushOperand(&ScaleAngleOperand);
		o_pStack->pushOperand(&RotAngleOperand);
		o_pStack->pushOperand(&CastColorOperand);
		o_pStack->pushOperand(&TextureDimensionOperand);
		o_pStack->pushOperand(&FrameRateOperand);
		l_Result = o_pStack->pushOperatorCpu((const MDOperatorCpu*)l_pMDOperatorBlendSkybox);
		DOME_ASSERT(DM_SUCC(l_Result));
	}
	
    return cacheResult(o_pStack, i_OutputSelector);
}

void            RCBlendSkyboxNode::finishLoad()
{
	
}



RC_NAMESPACE_END