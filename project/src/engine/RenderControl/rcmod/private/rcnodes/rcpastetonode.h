/*
    filename:       rcpastetonode.h
    author:         Ming Dong
    date:           2016-JUN-28
    description:    
*/
#pragma once

#include <rc/public/rc.h>

RC_NAMESPACE_BEGIN

class RCPasteToNode : public RCEffectNode
{
public:
    static RCEffectNode*    Create(RCEffect* i_pEffect);
    static DResult          Destroy(RCEffectNode* i_pEffectNode);

    RCPasteToNode(RCEffect* i_pEffect);
    virtual ~RCPasteToNode();

public:
    virtual DResult         buildMDEffect(MDEffect* o_pStack, Int i_OutputSelector);
    virtual void            finishLoad();

private:
    MDOperandValuePtr       m_SrcUVPos;
    MDOperandValuePtr       m_SrcUVSize;
    MDOperandValuePtr       m_TrgUVPos;
    MDOperandValuePtr       m_TrgUVSize;
};

RC_NAMESPACE_END

