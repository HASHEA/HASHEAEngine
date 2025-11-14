/*
    filename:       rcrenderwaterpretonode.h
    author:         Ming Dong
    date:           2016-Aug-24
    description:    
*/
#pragma once

#include <rc/public/rc.h>

RC_NAMESPACE_BEGIN

class RCRenderWaterPreToNode : public RCEffectNode
{
public:
    static RCEffectNode*    Create(RCEffect* i_pEffect);
    static DResult          Destroy(RCEffectNode* i_pEffectNode);

    RCRenderWaterPreToNode(RCEffect* i_pEffect);
    virtual ~RCRenderWaterPreToNode();

public:
    virtual DResult         buildMDEffect(MDEffect* o_pStack, Int i_OutputSelector);
    virtual void            finishLoad();

private:
    MDOperandValue          m_ColorSelector;
    MDOperandValue          m_DepthSelector;
};

RC_NAMESPACE_END

