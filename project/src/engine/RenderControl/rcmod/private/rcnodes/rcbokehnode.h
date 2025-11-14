#pragma once

#include <rc/public/rc.h>

RC_NAMESPACE_BEGIN

class RCBohekNode : public RCEffectNode
{
public:
    static RCEffectNode*    Create(RCEffect* i_pEffect);
    static DResult          Destroy(RCEffectNode* i_pEffectNode);

    RCBohekNode(RCEffect* i_pEffect);
    virtual ~RCBohekNode();

public:
    virtual DResult         buildMDEffect(MDEffect* o_pStack, Int i_OutputSelector);
    virtual void            finishLoad();

	virtual void			onReset() override;
private:
    MDOperandValuePtr	       DOFDepthsOperand;
	MDOperandValuePtr	       BokehBrightnessThresholdOperand;
	MDOperandValuePtr		   BokehBlurThresholdOperand;
	MDOperandValuePtr		   BokehFalloffOperand;
	MDOperandValuePtr		   MaxBokehSizeOperand;
};

RC_NAMESPACE_END

