#pragma once

#include <rc/public/rc.h>

RC_NAMESPACE_BEGIN

class RCAddNode : public RCEffectNode
{
public:
    static RCEffectNode*    Create(RCEffect* i_pEffect);
    static DResult          Destroy(RCEffectNode* i_pEffectNode);

    RCAddNode(RCEffect* i_pEffect);
    virtual ~RCAddNode();

public:
    virtual DResult         buildMDEffect(MDEffect* o_pStack, Int i_OutputSelector);
    virtual void            finishLoad();

	/*
private:
    MDOperandValuePtr       m_ConstValue;
	*/
};

RC_NAMESPACE_END

