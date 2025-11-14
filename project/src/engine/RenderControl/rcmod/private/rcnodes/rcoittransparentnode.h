/*
filename:       rcscenerendernode.h
author:         Ming Dong
date:           2016-Aug-24
description:
*/
#pragma once

#include <rc/public/rc.h>

RC_NAMESPACE_BEGIN

class RCOITTransparentNode : public RCEffectNode
{
public:
    static RCEffectNode*    Create(RCEffect* i_pEffect);
    static DResult          Destroy(RCEffectNode* i_pEffectNode);

    RCOITTransparentNode(RCEffect* i_pEffect);
    virtual ~RCOITTransparentNode();

public:
    virtual DResult         buildMDEffect(MDEffect* o_pStack, Int i_OutputSelector);
    virtual void            finishLoad();

private:
    MDOperandValue          m_ParamOperand;
    MDOperandValue          m_ViewportOperand;
    MDOperandValue          m_Color0Selector;
	MDOperandValue          m_Color1Selector;
    MDOperandValue          m_DepthSelector;
	MDOperandValue          m_RenderQueueOperand;
};

RC_NAMESPACE_END

