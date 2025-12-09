#include "pch.h"
#include "../../public/rcmod.h"
#include "rcconsttex3dnode.h"

RC_NAMESPACE_BEGIN

RCEffectNode*    RCConstTex3DNode::Create(RCEffect* i_pEffect)
{
    return DOME_New(RCConstTex3DNode)(i_pEffect);
}

DResult          RCConstTex3DNode::Destroy(RCEffectNode* i_pEffectNode)
{
    DOME_Del(i_pEffectNode);
    return R_SUCCESS;
}

RCConstTex3DNode::RCConstTex3DNode(RCEffect* i_pEffect)
: RCEffectNode(i_pEffect)
, m_TexOperand(RCGlobal::k_SimpleTypeID_OSTexture3D)
{
    m_TexOperand.getDataPtr()->getValuePtr<OSTexture2D>()->set(-1, DM_NULL);
}

RCConstTex3DNode::~RCConstTex3DNode()
{
    OSTexture3D l_Tex;
    RCRenderer* l_pRenderer = getRCEffect()->getEffectManager()->getRenderer();

    if (m_TexOperand.getDataPtr()->getValuePtr<OSTexture3D>())
    {
        l_Tex = *m_TexOperand.getDataPtr()->getValuePtr<OSTexture3D>();
        if (l_Tex.isValid())
        {
            l_pRenderer->destroyTexture3D(l_Tex);
            m_TexOperand.getDataPtr()->getValuePtr<OSTexture2D>()->set(-1, DM_NULL);
        }
    }
}

DResult         RCConstTex3DNode::buildMDEffect(MDEffect* o_pStack, Int i_OutputSelector)
{
    DOME_ASSERT(i_OutputSelector == 0);

	RCRenderer* l_pRenderer = getRCEffect()->getEffectManager()->getRenderer();

	DResult l_Result;
	OSTexture3D l_Tex;
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

		if (m_TexOperand.getDataPtr()->getValuePtr<OSTexture3D>())
		{
			l_Tex = *m_TexOperand.getDataPtr()->getValuePtr<OSTexture3D>();
            if (l_Tex.isValid())
            {
                l_pRenderer->destroyTexture3D(l_Tex);
                m_TexOperand.getDataPtr()->getValuePtr<OSTexture2D>()->set(-1, DM_NULL);
            }
		}


		l_Result = l_pRenderer->createTexture3DFromFile(l_Tex, l_FullFilePath);
		DOME_ASSERT(DM_SUCC(l_Result));

        if(DM_SUCC(l_Result))
		    m_TexOperand.getDataPtr()->set(RCGlobal::k_SimpleTypeID_OSTexture3D, &l_Tex);
	}
	
	o_pStack->pushOperand(&m_TexOperand);

    return R_SUCCESS;
}

void            RCConstTex3DNode::finishLoad()
{
    RCRenderer* l_pRenderer = getRCEffect()->getEffectManager()->getRenderer();

    DResult l_Result;
    OSTexture3D l_Tex;
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

    l_Result = l_pRenderer->createTexture3DFromFile(l_Tex, l_FullFilePath);
    DOME_ASSERT(DM_SUCC(l_Result));

    if(DM_SUCC(l_Result))
        m_TexOperand.getDataPtr()->set(RCGlobal::k_SimpleTypeID_OSTexture3D, &l_Tex);
}


RC_NAMESPACE_END