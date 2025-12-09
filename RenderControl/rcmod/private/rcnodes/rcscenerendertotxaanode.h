#pragma once

#include <rc/public/rc.h>

RC_NAMESPACE_BEGIN

class RCSceneRenderToTXAANode : public RCEffectNode
{
public:
    static RCEffectNode*    Create(RCEffect* i_pEffect);
    static DResult          Destroy(RCEffectNode* i_pEffectNode);

    RCSceneRenderToTXAANode(RCEffect* i_pEffect);
    virtual ~RCSceneRenderToTXAANode();

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

