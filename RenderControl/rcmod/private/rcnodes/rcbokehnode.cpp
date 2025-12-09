#include "pch.h"
#include "../../public/rcmod.h"
#include "rcbokehnode.h"

RC_NAMESPACE_BEGIN

RCEffectNode*    RCBohekNode::Create(RCEffect* i_pEffect)
{
    return DOME_New(RCBohekNode)(i_pEffect);
}

DResult          RCBohekNode::Destroy(RCEffectNode* i_pEffectNode)
{
    DOME_Del(i_pEffectNode);
    return R_SUCCESS;
}

RCBohekNode::RCBohekNode(RCEffect* i_pEffect)
: RCEffectNode(i_pEffect)
, DOFDepthsOperand(DM_NULL)
, BokehBrightnessThresholdOperand(DM_NULL)
, BokehBlurThresholdOperand(DM_NULL)
, BokehFalloffOperand(DM_NULL)
, MaxBokehSizeOperand(DM_NULL)
{
	
}

RCBohekNode::~RCBohekNode()
{
	
}

// Main Work is Done Here.
DResult         RCBohekNode::buildMDEffect(MDEffect* o_pStack, Int i_OutputSelector)
{
    DOME_ASSERT(i_OutputSelector == 0);
	DOME_ASSERT(getParamType(0) == RCGlobal::k_SimpleTypeID_DVector4f);
	DOME_ASSERT(getParamType(1) == RCGlobal::k_SimpleTypeID_F32);
	DOME_ASSERT(getParamType(2) == RCGlobal::k_SimpleTypeID_F32);
	DOME_ASSERT(getParamType(3) == RCGlobal::k_SimpleTypeID_F32);
	DOME_ASSERT(getParamType(4) == RCGlobal::k_SimpleTypeID_F32);

	static const DStringHash k_MDOperator_MDDeviceZToLinearZ("MDDeviceZToLinearZ");
    const MDOperator* l_pMDDeviceZToLinearZ = RCManager::Instance().getMDOperator(k_MDOperator_MDDeviceZToLinearZ);
	
	static const DStringHash k_MDOperator_MDDepthBlurGeneration("MDDepthBlurGeneration");
    const MDOperator* l_pMDDepthBlurGeneration = RCManager::Instance().getMDOperator(k_MDOperator_MDDepthBlurGeneration);
	
	static const DStringHash k_MDOperator_MDBokeh("MDBokeh");
    const MDOperator* l_pMDBokeh = RCManager::Instance().getMDOperator(k_MDOperator_MDBokeh);
	
	DOFDepthsOperand.set(getParam(0));
	BokehBrightnessThresholdOperand.set(getParam(1));
	BokehBlurThresholdOperand.set(getParam(2));
	BokehFalloffOperand.set(getParam(3));
	MaxBokehSizeOperand.set(getParam(4));

	DResult l_Result;
	{
		executePushInput(o_pStack, 0);

		executePushInput(o_pStack, 1);
		executePushInput(o_pStack, 3);
		l_Result = o_pStack->pushOperatorGpu((const MDOperatorGpu*)l_pMDDeviceZToLinearZ);
		DOME_ASSERT(DM_SUCC(l_Result));

		o_pStack->pushOperand(&DOFDepthsOperand);
		l_Result = o_pStack->pushOperatorGpu((const MDOperatorGpu*)l_pMDDepthBlurGeneration);
		DOME_ASSERT(DM_SUCC(l_Result));

		executePushInput(o_pStack, 2);
		o_pStack->pushOperand(&BokehBrightnessThresholdOperand);
		o_pStack->pushOperand(&BokehBlurThresholdOperand);
		o_pStack->pushOperand(&BokehFalloffOperand);
		o_pStack->pushOperand(&MaxBokehSizeOperand);
		l_Result = o_pStack->pushOperatorCpu((const MDOperatorCpu*)l_pMDBokeh);
		DOME_ASSERT(DM_SUCC(l_Result));

	}
	
    return cacheResult(o_pStack, i_OutputSelector);
}

void            RCBohekNode::finishLoad()
{
	
}

void			RCBohekNode::onReset()
{
	RCEffectManager* l_pRCEffectMgr = getRCEffect()->getEffectManager();
	const static DStringHash k_KEY_ScenePlugin("RCPI_Scene");
	RCPI_Scene* l_pScenePlugin = (RCPI_Scene*)l_pRCEffectMgr->getPlugin(k_KEY_ScenePlugin);

	l_pScenePlugin->ReleaseBokehResource();

}

RC_NAMESPACE_END