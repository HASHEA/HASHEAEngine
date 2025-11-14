/*
    filename:       rctonemappingnode.h
    author:         Ming Dong
    date:           2016-Aug-11
    description:    
*/
#pragma once

#include <rc/public/rc.h>

RC_NAMESPACE_BEGIN

class RCToneMappingNode : public RCEffectNode
{
public:
    static RCEffectNode*    Create(RCEffect* i_pEffect);
    static DResult          Destroy(RCEffectNode* i_pEffectNode);

    RCToneMappingNode(RCEffect* i_pEffect);
    virtual ~RCToneMappingNode();

public:
    virtual DResult         buildMDEffect(MDEffect* o_pStack, Int i_OutputSelector);
    virtual void            finishLoad();

private:
    MDOperandValuePtr       m_LumRangeLeftValue;
    MDOperandValuePtr       m_LumRangeRightValue;
};

RC_NAMESPACE_END

