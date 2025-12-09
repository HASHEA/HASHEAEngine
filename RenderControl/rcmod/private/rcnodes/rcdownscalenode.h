#pragma once

#include <rc/public/rc.h>

RC_NAMESPACE_BEGIN

class RCDownScaleNode : public RCEffectNode
{
public:
    static RCEffectNode*    Create(RCEffect* i_pEffect);
    static DResult          Destroy(RCEffectNode* i_pEffectNode);

    RCDownScaleNode(RCEffect* i_pEffect);
    virtual ~RCDownScaleNode();

public:
    virtual DResult         buildMDEffect(MDEffect* o_pStack, Int i_OutputSelector);
    virtual void            finishLoad();

private:
};


RC_NAMESPACE_END