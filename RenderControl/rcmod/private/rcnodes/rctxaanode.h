/*
    filename:       rctxaanode.h
    author:         Ming Dong
    date:           2016-Aug-18
    description:    
*/
#pragma once

#include <rc/public/rc.h>

RC_NAMESPACE_BEGIN

class RCTXAANode : public RCEffectNode
{
public:
    static RCEffectNode*    Create(RCEffect* i_pEffect);
    static DResult          Destroy(RCEffectNode* i_pEffectNode);

    RCTXAANode(RCEffect* i_pEffect);
    virtual ~RCTXAANode();

public:
    virtual DResult         buildMDEffect(MDEffect* o_pStack, Int i_OutputSelector);
    virtual void            finishLoad();

private:
};

RC_NAMESPACE_END

