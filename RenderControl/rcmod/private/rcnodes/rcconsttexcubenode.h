#pragma once

#include <rc/public/rc.h>

RC_NAMESPACE_BEGIN

class RCConstTexCubeNode : public RCEffectNode
{
public:
    static RCEffectNode*    Create(RCEffect* i_pEffect);
    static DResult          Destroy(RCEffectNode* i_pEffectNode);

    RCConstTexCubeNode(RCEffect* i_pEffect);
    virtual ~RCConstTexCubeNode();

public:
    virtual DResult         buildMDEffect(MDEffect* o_pStack, Int i_OutputSelector);
    virtual void            finishLoad();

private:
    MDOperandValue          m_TexOperand;
	DString					m_LoadedTextureName;
};


RC_NAMESPACE_END