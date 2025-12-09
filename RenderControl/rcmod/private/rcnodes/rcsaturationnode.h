#pragma once

#include <rc/public/rc.h>

RC_NAMESPACE_BEGIN

class RCSaturationNode : public RCEffectNode
{
public:
    static RCEffectNode*    Create(RCEffect* i_pEffect);
    static DResult          Destroy(RCEffectNode* i_pEffectNode);

    RCSaturationNode(RCEffect* i_pEffect);
    virtual ~RCSaturationNode();

public:
    virtual DResult         buildMDEffect(MDEffect* o_pStack, Int i_OutputSelector);
    virtual void            finishLoad();

private:
    MDOperandValuePtr       m_ConstValue;
	
	
};

RC_NAMESPACE_END

