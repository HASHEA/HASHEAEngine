/*
    filename:       rcresolveddepthinputnode.h
    author:         Ming Dong
    date:           2017-Jun-29
    description:    
*/
#pragma once

#include <rc/public/rc.h>

RC_NAMESPACE_BEGIN

class RCResolvedDepthInputNode : public RCEffectNode
{
public:
    static RCEffectNode*    Create(RCEffect* i_pEffect);
    static DResult          Destroy(RCEffectNode* i_pEffectNode);

    RCResolvedDepthInputNode(RCEffect* i_pEffect);
    virtual ~RCResolvedDepthInputNode();

public:
    virtual DResult         buildMDEffect(MDEffect* o_pStack, Int i_OutputSelector);
    virtual void            finishLoad();

private:
    MDOperandValuePtr       m_DepthTexture;
};

RC_NAMESPACE_END

