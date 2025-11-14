#pragma once

#include <rc/public/rc.h>

RC_NAMESPACE_BEGIN

class RCSMAANode : public RCEffectNode
{
public:
    static RCEffectNode*    Create(RCEffect* i_pEffect);
    static DResult          Destroy(RCEffectNode* i_pEffectNode);

    RCSMAANode(RCEffect* i_pEffect);
    virtual ~RCSMAANode();

public:
    virtual DResult         buildMDEffect(MDEffect* o_pStack, Int i_OutputSelector);
    virtual void            finishLoad();

	
private:
    MDOperandValue	       m_AreaTexOperand;
	MDOperandValue	       m_SearchTexOperand;
	MDOperandValue		   m_GamaValue;
};

RC_NAMESPACE_END

