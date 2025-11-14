/*
    filename:       rcaddconstnode.h
    author:         Ming Dong
    date:           2016-JUN-27
    description:    
*/
#pragma once

#include <rc/public/rc.h>

RC_NAMESPACE_BEGIN

class RCAddConstNode : public RCEffectNode
{
public:
    static RCEffectNode*    Create(RCEffect* i_pEffect);
    static DResult          Destroy(RCEffectNode* i_pEffectNode);

    RCAddConstNode(RCEffect* i_pEffect);
    virtual ~RCAddConstNode();

public:
    virtual DResult         buildMDEffect(MDEffect* o_pStack, Int i_OutputSelector);
    virtual void            finishLoad();

private:
    MDOperandValuePtr       m_ConstColorValue;
};

RC_NAMESPACE_END

