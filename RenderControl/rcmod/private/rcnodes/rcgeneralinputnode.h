/*
    filename:       rcgeneralinputnode.h
    author:         Ming Dong
    date:           2016-Aug-15
    description:    
*/
#pragma once

#include <rc/public/rc.h>

RC_NAMESPACE_BEGIN

class RCGeneralInputNode : public RCEffectNode
{
public:
    static RCEffectNode*    Create(RCEffect* i_pEffect);
    static DResult          Destroy(RCEffectNode* i_pEffectNode);

    RCGeneralInputNode(RCEffect* i_pEffect);
    virtual ~RCGeneralInputNode();

public:
    virtual DResult         buildMDEffect(MDEffect* o_pStack, Int i_OutputSelector);
    virtual void            finishLoad();

private:
    MDOperandValuePtr       m_Operand;
};


RC_NAMESPACE_END