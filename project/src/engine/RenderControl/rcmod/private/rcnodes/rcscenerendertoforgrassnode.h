/*
    filename:       rcscenerendertoforgrassnode.h
    author:         Ming Dong
    date:           2021-MAR-31
    description:    
*/
#pragma once

#include <rc/public/rc.h>

RC_NAMESPACE_BEGIN

class RCSceneRenderToForGrassNode : public RCEffectNode
{
public:
    static RCEffectNode*    Create(RCEffect* i_pEffect);
    static DResult          Destroy(RCEffectNode* i_pEffectNode);

    RCSceneRenderToForGrassNode(RCEffect* i_pEffect);
    virtual ~RCSceneRenderToForGrassNode();

public:
    virtual DResult         buildMDEffect(MDEffect* o_pStack, Int i_OutputSelector);
    virtual void            finishLoad();

private:
    MDOperandValue          m_ParamOperand;
    MDOperandValue          m_ViewportOperand;
    MDOperandValue          m_ColorSelector;
    MDOperandValue          m_DepthSelector;
};

RC_NAMESPACE_END

