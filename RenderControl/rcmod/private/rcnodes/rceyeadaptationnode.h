#pragma once

#include <rc/public/rc.h>

RC_NAMESPACE_BEGIN

class RCEyeAdaptationNode : public RCEffectNode
{
public:
    static RCEffectNode*    Create(RCEffect* i_pEffect);
    static DResult          Destroy(RCEffectNode* i_pEffectNode);

    RCEyeAdaptationNode(RCEffect* i_pEffect);
    virtual ~RCEyeAdaptationNode();

public:
    virtual DResult         buildMDEffect(MDEffect* o_pStack, Int i_OutputSelector);
    virtual void            finishLoad();

private:
    MDOperandValue		m_Frame0;
	MDOperandValue		m_Frame1;
	Bool				m_bTextureCreated;
};

RC_NAMESPACE_END

