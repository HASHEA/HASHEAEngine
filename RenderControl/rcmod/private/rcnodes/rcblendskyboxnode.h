#pragma once

#include <rc/public/rc.h>

RC_NAMESPACE_BEGIN

class RCBlendSkyboxNode : public RCEffectNode
{
public:
    static RCEffectNode*    Create(RCEffect* i_pEffect);
    static DResult          Destroy(RCEffectNode* i_pEffectNode);

    RCBlendSkyboxNode(RCEffect* i_pEffect);
    virtual ~RCBlendSkyboxNode();

public:
    virtual DResult         buildMDEffect(MDEffect* o_pStack, Int i_OutputSelector);
    virtual void            finishLoad();


private:
    MDOperandValuePtr	       PosDirOperand;
	MDOperandValuePtr	       ScaleAngleOperand;
	MDOperandValuePtr		   RotAngleOperand;
	MDOperandValuePtr		   CastColorOperand;
    MDOperandValuePtr		   TextureDimensionOperand;
    MDOperandValuePtr		   FrameRateOperand;
};

RC_NAMESPACE_END

