#pragma once

#include <rc/public/rc.h>

RC_NAMESPACE_BEGIN

class RCMulConstNode : public RCEffectNode
{
public:
    static RCEffectNode*    Create(RCEffect* i_pEffect);
    static DResult          Destroy(RCEffectNode* i_pEffectNode);

    RCMulConstNode(RCEffect* i_pEffect);
    virtual ~RCMulConstNode();

public:
    virtual DResult         buildMDEffect(MDEffect* o_pStack, Int i_OutputSelector);
    virtual void            finishLoad();

	
private:
    MDOperandValuePtr       m_ConstValue;
	
};

RC_NAMESPACE_END

