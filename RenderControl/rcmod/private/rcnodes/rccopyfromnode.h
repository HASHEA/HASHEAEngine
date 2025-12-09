/*
    filename:       rccopyfromnode.h
    author:         Ming Dong
    date:           2016-JUN-28
    description:    
*/
#pragma once

#include <rc/public/rc.h>

RC_NAMESPACE_BEGIN

class RCCopyFromNode : public RCEffectNode
{
public:
    static RCEffectNode*    Create(RCEffect* i_pEffect);
    static DResult          Destroy(RCEffectNode* i_pEffectNode);

    RCCopyFromNode(RCEffect* i_pEffect);
    virtual ~RCCopyFromNode();

public:
    virtual DResult         buildMDEffect(MDEffect* o_pStack, Int i_OutputSelector);
    virtual void            finishLoad();

private:
    MDOperandValuePtr       m_UVPos;
    MDOperandValuePtr       m_UVSize;
};

RC_NAMESPACE_END

