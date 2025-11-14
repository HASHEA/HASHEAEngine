#pragma once

#include <rc/public/rc.h>

RC_NAMESPACE_BEGIN

class RCClampNode : public RCEffectNode
{
public:
    static RCEffectNode*    Create(RCEffect* i_pEffect);
    static DResult          Destroy(RCEffectNode* i_pEffectNode);

    RCClampNode(RCEffect* i_pEffect);
    virtual ~RCClampNode();

public:
    virtual DResult         buildMDEffect(MDEffect* o_pStack, Int i_OutputSelector);
    virtual void            finishLoad();

private:
    MDOperandValuePtr       m_ConstValue;
	MDOperandValuePtr		m_ConstValue1;
	
};

RC_NAMESPACE_END

