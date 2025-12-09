#pragma once

#include <rc/public/rc.h>

RC_NAMESPACE_BEGIN

class RCSubNode : public RCEffectNode
{
public:
    static RCEffectNode*    Create(RCEffect* i_pEffect);
    static DResult          Destroy(RCEffectNode* i_pEffectNode);

    RCSubNode(RCEffect* i_pEffect);
    virtual ~RCSubNode();

public:
    virtual DResult         buildMDEffect(MDEffect* o_pStack, Int i_OutputSelector);
    virtual void            finishLoad();

};

RC_NAMESPACE_END

