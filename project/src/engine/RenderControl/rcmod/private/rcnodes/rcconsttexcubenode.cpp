#include "pch.h"
#include "../../public/rcmod.h"
#include "rcconsttexcubenode.h"

RC_NAMESPACE_BEGIN

RCEffectNode*    RCConstTexCubeNode::Create(RCEffect* i_pEffect)
{
    return DOME_New(RCConstTexCubeNode)(i_pEffect);
}

DResult          RCConstTexCubeNode::Destroy(RCEffectNode* i_pEffectNode)
{
    DOME_Del(i_pEffectNode);
    return R_SUCCESS;
}

RCConstTexCubeNode::RCConstTexCubeNode(RCEffect* i_pEffect)
: RCEffectNode(i_pEffect)
, m_TexOperand(RCGlobal::k_SimpleTypeID_OSTextureCube)
{
    m_TexOperand.getDataPtr()->getValuePtr<OSTextureCube>()->set(-1, DM_NULL);
}

RCConstTexCubeNode::~RCConstTexCubeNode()
{
    OSTextureCube l_Tex;
    RCRenderer* l_pRenderer = getRCEffect()->getEffectManager()->getRenderer();

    if (m_TexOperand.getDataPtr()->getValuePtr<OSTextureCube>())
    {
        l_Tex = *m_TexOperand.getDataPtr()->getValuePtr<OSTextureCube>();
        if (l_Tex.isValid())
        {
            l_pRenderer->destroyTextureCube(l_Tex);
            m_TexOperand.getDataPtr()->getValuePtr<OSTextureCube>()->set(-1, DM_NULL);
        }
    }
}

DResult         RCConstTexCubeNode::buildMDEffect(MDEffect* o_pStack, Int i_OutputSelector)
{
    DOME_ASSERT(i_OutputSelector == 0);

	RCRenderer* l_pRenderer = getRCEffect()->getEffectManager()->getRenderer();

	DResult l_Result;
	OSTextureCube l_Tex;
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

		if (m_TexOperand.getDataPtr()->getValuePtr<OSTextureCube>())
		{
			l_Tex = *m_TexOperand.getDataPtr()->getValuePtr<OSTextureCube>();
            if (l_Tex.isValid())
            {
                l_pRenderer->destroyTextureCube(l_Tex);
                m_TexOperand.getDataPtr()->getValuePtr<OSTextureCube>()->set(-1, DM_NULL);
            }
		}


		l_Result = l_pRenderer->createTextureCubeFromFile(l_Tex, l_FullFilePath);
		DOME_ASSERT(DM_SUCC(l_Result));

        if(DM_SUCC(l_Result))
		    m_TexOperand.getDataPtr()->set(RCGlobal::k_SimpleTypeID_OSTextureCube, &l_Tex);
	}
	
	o_pStack->pushOperand(&m_TexOperand);

    return R_SUCCESS;
}

void            RCConstTexCubeNode::finishLoad()
{
    RCRenderer* l_pRenderer = getRCEffect()->getEffectManager()->getRenderer();

    DResult l_Result;
    OSTextureCube l_Tex;
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

    l_Result = l_pRenderer->createTextureCubeFromFile(l_Tex, l_FullFilePath);
    DOME_ASSERT(DM_SUCC(l_Result));

    if(DM_SUCC(l_Result))
        m_TexOperand.getDataPtr()->set(RCGlobal::k_SimpleTypeID_OSTextureCube, &l_Tex);
}


RC_NAMESPACE_END