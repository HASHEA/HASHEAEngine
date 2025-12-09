/*
    filename:       rcdistorationnode.h
    author:         Ming Dong
    date:           2016-Aug-24
    description:    
*/
#pragma once

#include <rc/public/rc.h>

RC_NAMESPACE_BEGIN

class RCDistorationNode : public RCEffectNode
{
public:
    static RCEffectNode*    Create(RCEffect* i_pEffect);
    static DResult          Destroy(RCEffectNode* i_pEffectNode);

    RCDistorationNode(RCEffect* i_pEffect);
    virtual ~RCDistorationNode();

public:
    virtual DResult         buildMDEffect(MDEffect* o_pStack, Int i_OutputSelector);
    virtual void            finishLoad();

private:
};

RC_NAMESPACE_END

