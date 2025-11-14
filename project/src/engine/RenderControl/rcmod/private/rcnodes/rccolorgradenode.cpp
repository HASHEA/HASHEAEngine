#include "pch.h"
#include "../../public/rcmod.h"
#include "rcColorGradenode.h"

RC_NAMESPACE_BEGIN

RCEffectNode*    RCColorGradeNode::Create(RCEffect* i_pEffect)
{
    return DOME_New(RCColorGradeNode)(i_pEffect);
}

DResult          RCColorGradeNode::Destroy(RCEffectNode* i_pEffectNode)
{
    DOME_Del(i_pEffectNode);
    return R_SUCCESS;
}

RCColorGradeNode::RCColorGradeNode(RCEffect* i_pEffect)
: RCEffectNode(i_pEffect)
, m_LutTexOperand(RCGlobal::k_SimpleTypeID_OSTexture2D)
{
    m_LutTexOperand.getDataPtr()->getValuePtr<OSTexture2D>()->set(-1, DM_NULL);
}

RCColorGradeNode::~RCColorGradeNode()
{
	OSTexture2D l_LutTex;
    RCRenderer* l_pRenderer = getRCEffect()->getEffectManager()->getRenderer();

    if (m_LutTexOperand.getDataPtr()->getValuePtr<OSTexture2D>())
    {
        l_LutTex = *m_LutTexOperand.getDataPtr()->getValuePtr<OSTexture2D>();
        if (l_LutTex.isValid())
        {
            l_pRenderer->destroyTexture2D(l_LutTex);
            m_LutTexOperand.getDataPtr()->getValuePtr<OSTexture2D>()->set(-1, DM_NULL);
        }
    }
}

// Main Work is Done Here.
DResult         RCColorGradeNode::buildMDEffect(MDEffect* o_pStack, Int i_OutputSelector)
{
    DOME_ASSERT(i_OutputSelector == 0);

   	RCRenderer* l_pRenderer = getRCEffect()->getEffectManager()->getRenderer();

	DResult l_Result;
	OSTexture2D l_Tex;
	const DSimpleTypedValue* l_FilePathValue = getParam(0);
	DOME_ASSERT(l_FilePathValue && l_FilePathValue->getTypeID() == RCGlobal::k_SimpleTypeID_DString);
	DString l_FilePath = l_FilePathValue->getDString();
	if (l_FilePath != m_LoadedTextureName)
	{
		m_LoadedTextureName = l_FilePath;
		DString l_FullFilePath;
        if (l_FilePath.isBeginWith("data"))
            l_FullFilePath = l_FilePath;
        else
        {
		    l_pRenderer->getDataPath(l_FullFilePath);
		    l_FullFilePath += l_FilePath;
        }

		if (m_LutTexOperand.getDataPtr()->getValuePtr<OSTexture2D>())
		{
			l_Tex = *m_LutTexOperand.getDataPtr()->getValuePtr<OSTexture2D>();
            if (l_Tex.isValid())
            {
			    l_pRenderer->destroyTexture2D(l_Tex);
                m_LutTexOperand.getDataPtr()->getValuePtr<OSTexture2D>()->set(-1, DM_NULL);
            }
		}


		l_Result = l_pRenderer->createTexture2DFromFile(l_Tex, l_FullFilePath);
		DOME_ASSERT(DM_SUCC(l_Result));

        if(DM_SUCC(l_Result))
		    m_LutTexOperand.getDataPtr()->set(RCGlobal::k_SimpleTypeID_OSTexture2D, &l_Tex);
	}


	// Set Input
    executePushInput(o_pStack, 0);

	// Get and Set Value
	o_pStack->pushOperand( &m_LutTexOperand );

	// SGet and Set Operation
    static const DStringHash k_MDOperator_MDOperatorName("MDColorGrade");
    const MDOperator* l_pMDOperator = RCManager::Instance().getMDOperator(k_MDOperator_MDOperatorName);
    l_Result = o_pStack->pushOperatorGpu((const MDOperatorGpu*)l_pMDOperator);
    DOME_ASSERT(DM_SUCC(l_Result));

    return cacheResult(o_pStack, i_OutputSelector);
}

void            RCColorGradeNode::finishLoad()
{
	RCRenderer* l_pRenderer = getRCEffect()->getEffectManager()->getRenderer();

    DResult l_Result;
    OSTexture2D l_LutTex;
    const DSimpleTypedValue* l_strTexPathValue = getParam(0);
    DOME_ASSERT(l_strTexPathValue && l_strTexPathValue->getTypeID() == RCGlobal::k_SimpleTypeID_DString);
    DString l_FilePath = l_strTexPathValue->getDString();
    m_LoadedTextureName = l_FilePath;

    DString l_FullFilePath;
    if (l_FilePath.isBeginWith("data"))
        l_FullFilePath = l_FilePath;
    else
    {
        l_pRenderer->getDataPath(l_FullFilePath);
        l_FullFilePath += l_FilePath;
    }

    l_Result = l_pRenderer->createTexture2DFromFile( l_LutTex, l_FullFilePath );
    DOME_ASSERT(DM_SUCC(l_Result));

    if(DM_SUCC(l_Result))
        m_LutTexOperand.getDataPtr()->set(RCGlobal::k_SimpleTypeID_OSTexture2D, &l_LutTex);
}


RC_NAMESPACE_END