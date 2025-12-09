#pragma once

#include <rc/public/rc.h>

RC_NAMESPACE_BEGIN

class RCChannelBooleanNode : public RCEffectNode
{
public:
    static RCEffectNode*    Create(RCEffect* i_pEffect);
    static DResult          Destroy(RCEffectNode* i_pEffectNode);

    RCChannelBooleanNode(RCEffect* i_pEffect);
    virtual ~RCChannelBooleanNode();

public:
    virtual DResult         buildMDEffect(MDEffect* o_pStack, Int i_OutputSelector);
    virtual void            finishLoad();

	
private:
    MDOperandValue       m_ConstValue;
	MDOperandValue       m_ConstValue1;
	
};

RC_NAMESPACE_END

