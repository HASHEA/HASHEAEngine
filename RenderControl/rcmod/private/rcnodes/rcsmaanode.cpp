#include "pch.h"
#include "../../public/rcmod.h"
#include "rcSMAAnode.h"

RC_NAMESPACE_BEGIN

RCEffectNode*    RCSMAANode::Create(RCEffect* i_pEffect)
{
    return DOME_New(RCSMAANode)(i_pEffect);
}

DResult          RCSMAANode::Destroy(RCEffectNode* i_pEffectNode)
{
    DOME_Del(i_pEffectNode);
    return R_SUCCESS;
}

RCSMAANode::RCSMAANode(RCEffect* i_pEffect)
: RCEffectNode(i_pEffect)
, m_AreaTexOperand(RCGlobal::k_SimpleTypeID_OSTexture2D)
, m_SearchTexOperand(RCGlobal::k_SimpleTypeID_OSTexture2D)
, m_GamaValue(RCGlobal::k_SimpleTypeID_F32)
{
	m_GamaValue.getDataPtr()->setF32(1.0f / 2.2f);
}

RCSMAANode::~RCSMAANode()
{
	OSTexture2D l_Tex;
	RCRenderer* l_pRenderer = getRCEffect()->getEffectManager()->getRenderer();

	if (m_AreaTexOperand.getDataPtr()->getValuePtr<OSTexture2D>())
	{
		l_Tex = *m_AreaTexOperand.getDataPtr()->getValuePtr<OSTexture2D>();
		l_pRenderer->destroyTexture2D(l_Tex);
	}

	if (m_SearchTexOperand.getDataPtr()->getValuePtr<OSTexture2D>())
	{
		l_Tex = *m_SearchTexOperand.getDataPtr()->getValuePtr<OSTexture2D>();
		l_pRenderer->destroyTexture2D(l_Tex);
	}
}

// Main Work is Done Here.
DResult         RCSMAANode::buildMDEffect(MDEffect* o_pStack, Int i_OutputSelector)
{
    DOME_ASSERT(i_OutputSelector == 0);

	static const DStringHash k_MDOperator_MDColorEdgeDetection("MDColorEdgeDetection");
    const MDOperator* l_pMDColorEdgeDetection = RCManager::Instance().getMDOperator(k_MDOperator_MDColorEdgeDetection);
	
	static const DStringHash k_MDOperator_MDNeighborhoodBlending("MDNeighborhoodBlending");
    const MDOperator* l_pMDNeighborhoodBlending = RCManager::Instance().getMDOperator(k_MDOperator_MDNeighborhoodBlending);
	
	static const DStringHash k_MDOperator_MDBlendingWeightCalculation("MDBlendingWeightCalculation");
    const MDOperator* l_pMDBlendingWeightCalculation = RCManager::Instance().getMDOperator(k_MDOperator_MDBlendingWeightCalculation);
	
	DResult l_Result;
	/// Neighborhood Blending
	{
		executePushInput(o_pStack, 0);
		
		/// BlendingWeightCalculation
		{
			/// Edge Detection
			{
				executePushInput(o_pStack, 0);
				l_Result = o_pStack->pushOperatorGpu((const MDOperatorGpu*)l_pMDColorEdgeDetection);
				DOME_ASSERT(DM_SUCC(l_Result));
			}
			
			/// AreaTex
			o_pStack->pushOperand(&m_AreaTexOperand);
			
			/// SearchTex
			o_pStack->pushOperand(&m_SearchTexOperand);
			
			l_Result = o_pStack->pushOperatorGpu((const MDOperatorGpu*)l_pMDBlendingWeightCalculation);	
			DOME_ASSERT(DM_SUCC(l_Result));
		}
		
		l_Result = o_pStack->pushOperatorGpu((const MDOperatorGpu*)l_pMDNeighborhoodBlending);	
		DOME_ASSERT(DM_SUCC(l_Result));
	}
	
    return cacheResult(o_pStack, i_OutputSelector);
}

void            RCSMAANode::finishLoad()
{
	RCRenderer* l_pRenderer = getRCEffect()->getEffectManager()->getRenderer();

    DResult l_Result;
    OSTexture2D l_Tex;
    
    DString l_FullFilePath;
    l_pRenderer->getDataPath(l_FullFilePath);

    l_Result = l_pRenderer->createTexture2DFromFile(l_Tex, l_FullFilePath + "textures/AreaTex.dds");
    DOME_ASSERT(DM_SUCC(l_Result));

    m_AreaTexOperand.getDataPtr()->set(RCGlobal::k_SimpleTypeID_OSTexture2D, &l_Tex);
	
	l_Result = l_pRenderer->createTexture2DFromFile(l_Tex, l_FullFilePath + "textures/SearchTex.dds");
    DOME_ASSERT(DM_SUCC(l_Result));
	
	m_SearchTexOperand.getDataPtr()->set(RCGlobal::k_SimpleTypeID_OSTexture2D, &l_Tex);
}


RC_NAMESPACE_END