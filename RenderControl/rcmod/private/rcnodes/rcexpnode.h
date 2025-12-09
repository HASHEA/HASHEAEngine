#pragma once

#include <rc/public/rc.h>

RC_NAMESPACE_BEGIN

class RCExpNode : public RCEffectNode
{
public:
    static RCEffectNode*    Create(RCEffect* i_pEffect);
    static DResult          Destroy(RCEffectNode* i_pEffectNode);

    RCExpNode(RCEffect* i_pEffect);
    virtual ~RCExpNode();

public:
    virtual DResult         buildMDEffect(MDEffect* o_pStack, Int i_OutputSelector);
    virtual void            finishLoad();

};

RC_NAMESPACE_END

