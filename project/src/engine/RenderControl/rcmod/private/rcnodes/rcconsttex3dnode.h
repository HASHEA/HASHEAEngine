#pragma once

#include <rc/public/rc.h>

RC_NAMESPACE_BEGIN

class RCConstTex3DNode : public RCEffectNode
{
public:
    static RCEffectNode*    Create(RCEffect* i_pEffect);
    static DResult          Destroy(RCEffectNode* i_pEffectNode);

    RCConstTex3DNode(RCEffect* i_pEffect);
    virtual ~RCConstTex3DNode();

public:
    virtual DResult         buildMDEffect(MDEffect* o_pStack, Int i_OutputSelector);
    virtual void            finishLoad();

private:
    MDOperandValue          m_TexOperand;
	DString					m_LoadedTextureName;
};


RC_NAMESPACE_END