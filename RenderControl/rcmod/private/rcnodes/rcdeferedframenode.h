/*
    filename:       rcaddconstnode.h
    author:         Ming Dong
    date:           2016-JUN-27
    description:    
*/
#pragma once

#include <rc/public/rc.h>

RC_NAMESPACE_BEGIN

class RCDeferedFrameNode : public RCEffectNode
{
public:
    static RCEffectNode*    Create(RCEffect* i_pEffect);
    static DResult          Destroy(RCEffectNode* i_pEffectNode);

    RCDeferedFrameNode(RCEffect* i_pEffect);
    virtual ~RCDeferedFrameNode();

public:
    virtual DResult         buildMDEffect(MDEffect* o_pStack, Int i_OutputSelector);
    virtual void            finishLoad();

private:
    DSimpleTypeID                   m_ValueTypeID;
    TArray<DSimpleTypedValue>       m_ValuePool;
    MDOperandValuePtr               m_CurrFrameOperand;
    MDOperandValuePtr               m_LastFrameOperand;
};

RC_NAMESPACE_END

