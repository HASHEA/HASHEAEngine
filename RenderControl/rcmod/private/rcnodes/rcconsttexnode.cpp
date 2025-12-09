#include "pch.h"
#include "../../public/rcmod.h"
#include "rcconsttexnode.h"

RC_NAMESPACE_BEGIN

RCEffectNode*    RCConstTexNode::Create(RCEffect* i_pEffect)
{
    return DOME_New(RCConstTexNode)(i_pEffect);
}

DResult          RCConstTexNode::Destroy(RCEffectNode* i_pEffectNode)
{
    DOME_Del(i_pEffectNode);
    return R_SUCCESS;
}

RCConstTexNode::RCConstTexNode(RCEffect* i_pEffect)
: RCEffectNode(i_pEffect)
, m_TexOperand(RCGlobal::k_SimpleTypeID_OSTexture2D)
{
    m_TexOperand.getDataPtr()->getValuePtr<OSTexture2D>()->set(-1, DM_NULL);
}

RCConstTexNode::~RCConstTexNode()
{
    OSTexture2D l_Tex;
    RCRenderer* l_pRenderer = getRCEffect()->getEffectManager()->getRenderer();

    if (m_TexOperand.getDataPtr()->getValuePtr<OSTexture2D>())
    {
        l_Tex = *m_TexOperand.getDataPtr()->getValuePtr<OSTexture2D>();
        if (l_Tex.isValid())
        {
            l_pRenderer->destroyTexture2D(l_Tex);
            m_TexOperand.getDataPtr()->getValuePtr<OSTexture2D>()->set(-1, DM_NULL);
        }
    }
}

DResult         RCConstTexNode::buildMDEffect(MDEffect* o_pStack, Int i_OutputSelector)
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

		if (m_TexOperand.getDataPtr()->getValuePtr<OSTexture2D>())
		{
			l_Tex = *m_TexOperand.getDataPtr()->getValuePtr<OSTexture2D>();
            if (l_Tex.isValid())
            {
                l_pRenderer->destroyTexture2D(l_Tex);
                m_TexOperand.getDataPtr()->getValuePtr<OSTexture2D>()->set(-1, DM_NULL);
            }
		}


		l_Result = l_pRenderer->createTexture2DFromFile(l_Tex, l_FullFilePath);
		DOME_ASSERT(DM_SUCC(l_Result));

        if(DM_SUCC(l_Result))
		    m_TexOperand.getDataPtr()->set(RCGlobal::k_SimpleTypeID_OSTexture2D, &l_Tex);
	}

    if(m_TexOperand.getDataPtr()->getValuePtr<OSTexture2D>())
    {
        l_Tex = *m_TexOperand.getDataPtr()->getValuePtr<OSTexture2D>();
        l_pRenderer->refreshLoaded2DTexture(l_Tex);
    }


    o_pStack->pushOperand(&m_TexOperand);

    return R_SUCCESS;
}

void            RCConstTexNode::finishLoad()
{
    RCRenderer* l_pRenderer = getRCEffect()->getEffectManager()->getRenderer();

    DResult l_Result;
    OSTexture2D l_Tex;
    const DSimpleTypedValue* l_FilePathValue = getParam(0);
    DOME_ASSERT(l_FilePathValue && l_FilePathValue->getTypeID() == RCGlobal::k_SimpleTypeID_DString);
    DString l_FilePath = l_FilePathValue->getDString();
	m_LoadedTextureName = l_FilePath;
    DString l_FullFilePath;
    if (l_FilePath.isBeginWith("data"))
        l_FullFilePath = l_FilePath;
    else
    {
        l_pRenderer->getDataPath(l_FullFilePath);
        l_FullFilePath += l_FilePath;
    }


    l_Result = l_pRenderer->createTexture2DFromFile(l_Tex, l_FullFilePath);
    DOME_ASSERT(DM_SUCC(l_Result));

    if(DM_SUCC(l_Result))
        m_TexOperand.getDataPtr()->set(RCGlobal::k_SimpleTypeID_OSTexture2D, &l_Tex);
}


RC_NAMESPACE_END