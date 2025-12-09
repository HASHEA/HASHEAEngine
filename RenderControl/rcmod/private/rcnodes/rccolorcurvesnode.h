#pragma once

#include <rc/public/rc.h>

RC_NAMESPACE_BEGIN

class RCColorCurvesNode : public RCEffectNode
{
public:
    static RCEffectNode*    Create(RCEffect* i_pEffect);
    static DResult          Destroy(RCEffectNode* i_pEffectNode);

    RCColorCurvesNode(RCEffect* i_pEffect);
    virtual ~RCColorCurvesNode();

public:
    virtual DResult         buildMDEffect(MDEffect* o_pStack, Int i_OutputSelector);
    virtual void            finishLoad();

private:
    void                    updateLutTex();

    Int                     m_LutVersion;
    MDOperandValue          m_LutTexOperand;
};


RC_NAMESPACE_END