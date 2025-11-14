/*
    filename:       rcdepthinputnode.h
    author:         Ming Dong
    date:           2016-MAY-24
    description:    
*/
#pragma once

#include <rc/public/rc.h>

RC_NAMESPACE_BEGIN

class RCDepthInputNode : public RCEffectNode
{
public:
    static RCEffectNode*    Create(RCEffect* i_pEffect);
    static DResult          Destroy(RCEffectNode* i_pEffectNode);

    RCDepthInputNode(RCEffect* i_pEffect);
    virtual ~RCDepthInputNode();

public:
    virtual DResult         buildMDEffect(MDEffect* o_pStack, Int i_OutputSelector);
    virtual void            finishLoad();

private:
    MDOperandValuePtr       m_DepthTexture;
};

RC_NAMESPACE_END

