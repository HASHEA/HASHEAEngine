/*
    filename:       rcgbuffer0node.h
    author:         Ming Dong
    date:           2016-MAY-24
    description:    
*/
#pragma once

#include <rc/public/rc.h>

RC_NAMESPACE_BEGIN

class RCGBuffer0Node : public RCEffectNode
{
public:
    static RCEffectNode*    Create(RCEffect* i_pEffect);
    static DResult          Destroy(RCEffectNode* i_pEffectNode);

    RCGBuffer0Node(RCEffect* i_pEffect);
    virtual ~RCGBuffer0Node();

public:
    virtual DResult         buildMDEffect(MDEffect* o_pStack, Int i_OutputSelector);
    virtual void            finishLoad();

private:
    MDOperandValuePtr       m_GBuffer0Texture;
};

RC_NAMESPACE_END

